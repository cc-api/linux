#!/usr/bin/python3 -u
#Intel Next merge script used to generate intel-next kernel
#Requires tabulate library to be installed from pip and git
#manifest_in.json should be in the same directory as this script
#regen option (-r) also depends on the regen_configs.sh script in the same path as this script

import json
import subprocess
import sys
import getopt
import glob
import datetime
import re
import argparse
import os
import tabulate
import pygit2
import netrc
import concurrent.futures
import functools
import tabulate
import itertools
import more_itertools
from io import StringIO
manifest_in_path="manifest_in.json"
manifest_out_path="manifest.json"
script_name="Intel Next Merge script"
log_file_name="intel-next-merge-" + format(datetime.date.today()) + ".log"
patch_manifest="patch-manifest"
config_path="arch/x86/configs"
config_fragments = "intel_next_config_options.config"
rule = "------------------------------------------------------------------------"

config_files_to_val = ["dcg_x86_64_defconfig",
               "intel_next_generic_defconfig", 
               "intel_next_rpm_defconfig"]

PROXY = "http://proxy-us.intel.com:911"
DEFAULT_BRANDING="Intel Next"
BASE_JIRA_URL = "https://jira.devtools.intel.com/browse"

#config files to add to git
config_files = [config_fragments] + config_files_to_val
#if verbose mode is set output all cmds to stdout            
verbose_mode=False
#Global log file objs
log = None
patch = None

def create_repo(path="."):
    return pygit2.Repository(path)

def get_callbacks(path=None):
    if path is None:
        path = os.path.join(os.path.expanduser("~"), ".netrc")
        username, _, password = netrc.netrc(path).authenticators("github.com")
        return pygit2.RemoteCallbacks(
            credentials=pygit2.UserPass(username, password))

def get_sha(repo, credentials, remote_url, branch):
    remote = repo.remotes[sanitize_repo_name(remote_url)]
    return next(str(dct["oid"]) for dct in remote.ls_remotes(callbacks=credentials, proxy=PROXY)
        if dct["name"] == f"refs/heads/{branch}")

def add_absent_remotes(repo, manifest):
    branches = [dct for dct in manifest["topic_branches"] if dct["enabled"]]
    for dct in branches:
        remote_name = sanitize_repo_name(dct["repourl"])
        # if remote_name not in repo.remotes:
        try:
            repo.remotes[remote_name]
        except KeyError:
            print_and_log(f"{remote_name} is not in remotes list, adding...")
            repo.remotes.create(remote_name, dct["repourl"])

def get_all_shas(repo, credentials, manifest):
    def helper(dct):
        return {f"{sanitize_repo_name(dct['repourl'])}/{dct['branch']}" : 
                get_sha(repo, credentials, dct["repourl"], dct["branch"])}
    branches = [dct for dct in manifest["topic_branches"] if dct["enabled"]]
    add_absent_remotes(repo, manifest)
    with concurrent.futures.ThreadPoolExecutor() as pool:
        threads = [pool.submit(helper, dct) for dct in branches]
        return functools.reduce(
            dict.__or__, 
            (t.result() for t in concurrent.futures.as_completed(threads)), 
            {})

def get_local_shas(repo, manifest):
    branches = [dct for dct in manifest["topic_branches"] if dct["enabled"]]
    result = {}
    for dct in branches:
        branch_name = f"{sanitize_repo_name(dct['repourl'])}/{dct['branch']}"
        branch_obj = repo.branches.get(branch_name)
        result[branch_name] = str(branch_obj.target) if branch_obj is not None else None
    return result

def reconcile_shas(repo, credentials, manifest, debug=False):
    remote_shas = get_all_shas(repo, credentials, manifest)
    local_shas = get_local_shas(repo, manifest)
    branches = [dct for dct in manifest["topic_branches"] if dct["enabled"]]
    topic_branch_names = [dct["name"] for dct in branches]
    branch_names = [f"{sanitize_repo_name(dct['repourl'])}/{dct['branch']}" for dct in branches]
    remote_sha_list = [remote_shas[b] for b in branch_names]
    local_sha_list = [local_shas[b] for b in branch_names]
    if debug:
        print_and_log(tabulate.tabulate(
            zip(topic_branch_names, map(lambda s: s[-10:], remote_sha_list), map(lambda s: s[-10:] if s is not None else "", local_sha_list),
                (sha1 != sha2 for sha1, sha2 in zip(remote_sha_list, local_sha_list))),
            headers=["Branch Name", "Remote SHA", "Local SHA", "Fetch Needed?"],
            tablefmt="psql"))
    return [name for name, sha in local_shas.items() if remote_shas[name] != sha]

def improved_fetch(repo, credentials, manifest, debug=False):
    needed_remotes = [s.split("/", 1)[0] for s in reconcile_shas(repo, credentials, manifest, debug)]
    if needed_remotes:
        print_and_log("Fetching the following remotes:", *needed_remotes, sep="\n")
        run_shell_cmd(f"git fetch --multiple {' '.join(needed_remotes)}")
    else:
        print_and_log("No fetch needed.")


def fast_fetch_remotes(manifest_in, skip_fetch=False):
    repo = create_repo()
    callbacks = get_callbacks()
    if not skip_fetch:
        improved_fetch(repo, callbacks, manifest_in, True)
    for dct in manifest_in["topic_branches"]:
        branch_name = f"{sanitize_repo_name(dct['repourl'])}/{dct['branch']}"
        if not dct["stuck_at_ref"]:
            branch_obj = repo.branches.get(branch_name.strip())
            dct["rev"] = str(branch_obj.target) if branch_obj is not None else None
        else:
            dct["rev"] = dct["stuck_at_ref"]
    return manifest_in

def accessor(keys, *, predicate=bool, default=None):
    def inner(obj):
        for k in keys:
            try:
                obj = obj[k]
            except (IndexError, KeyError):
                return default
        return obj if predicate(obj) else default
    return inner

def generate_row(dct, func_dct):
    return { k : v(dct) for k, v in func_dct.items() }

def generate_table(manifest, tablefmt="github"):
    column_dct = {
        "Topic Branch" : accessor(["name"]),
        "Description" : accessor(["description"]),
        "Targeted platforms" : accessor(["platforms"], default="N/A"),
        "Contributor" : accessor(["contributor", 0, "name"]),
        "Email" : accessor(["contributor", 0, "email"]),
        "Repo URL" : accessor(["repourl"]),
        "Branch" : accessor(["branch"]),
        "Reference" : accessor(["rev"])
    }
    return tabulate.tabulate((
        generate_row(branch, column_dct) for branch in manifest["topic_branches"]
        if branch["enabled"]),
        tablefmt=tablefmt,
        headers="keys")

def get_commits(repo, ref, final_author):
    """
    Generates all commits until a commit has author `final_author`.
    """
    return itertools.takewhile(lambda commit: commit is not None and commit.author.name != final_author,
        more_itertools.iterate(lambda commit: commit.parents[0] if commit.parents else None,
            repo.get(ref)))

def contains_intel_next_commits(repo, ref):
    """
    Returns True if any commits in a topic branch contain a commit authored by Intel Next.
    """
    return any("Intel Next: Add release files" in commit.message
        for commit in get_commits(repo, ref, "Linus Torvalds"))

def check_manifest_intel_next(repo, manifest):
    """
    Raises an exception to fail the CI build if any of the enabled branches
    in the manifest contain an Intel Next commit.
    Explicitly avoids checking eywa, since it's a false positive.
    """
    for branch in manifest["topic_branches"]:
        if branch["enabled"] and contains_intel_next_commits(repo, branch["rev"]):
            raise RuntimeError(f"Branch {branch['name']} contains an Intel-Next commit!")


def run_shell_cmd(cmd):
   
    log.write("cmd:" +cmd+"\n")

    pobj = subprocess.Popen(cmd,
                     shell = True,
                     stdout = subprocess.PIPE,
                     stderr = subprocess.PIPE)
    #One issue here is that if it is a long git command we won't have output until it is done
    stdout,stderr = pobj.communicate()
    retcode = pobj.returncode
    stdout = stdout.decode('ascii')
    stderr = stderr.decode('ascii')
    log.write("STDOUT:\n"+stdout)
    log.write("STDERR:\n"+stderr)
    log.write("retcode:"+str(retcode) + "\n\n")
    if verbose_mode:
       print_and_log("cmd:" +cmd)
       print_and_log("STDOUT:"+stdout)
       print_and_log("STDERR:"+stderr)
    if retcode != 0:
        raise Exception(stderr)
    return stdout

def run_shell_cmd_output(cmd):
    print_and_log(run_shell_cmd(cmd))
    #this is a W/A so buildbot does not buffer stdout
    sys.stdout.flush()

def git_add_remote(name,repo_url,current_remotes):
    if name in current_remotes:
        run_shell_cmd("git remote set-url {} {}".format(name,repo_url))
    else:
        run_shell_cmd("git remote add {} {}".format(name,repo_url))
        current_remotes.append(name)
    return current_remotes

def git_fetch_remote(name):
    print_and_log("Fetching {}".format(name))
    run_shell_cmd("git fetch --prune --force {}".format(name))
    sys.stdout.flush()

def sanitize_repo_name(repo_url):
    #This is a special case for Len's branch which has a ~ in it
    repo_url = repo_url.replace("~","")
    clean_repo_url = re.sub('[^A-Za-z0-9]+','_',repo_url)
    return clean_repo_url

def read_manifest(path):
    mainfest = None
    with open(path) as f:
        manifest = json.loads(f.read())
    return manifest

def add_remotes(manifest):
    current_remotes = run_shell_cmd("git remote")
    current_remotes = current_remotes.split("\n")
   
    main_branch = manifest["master_branch"]
    branches = [main_branch]+ manifest["topic_branches"]
    done_fetch ={}
    #Add remotes
   
    for branch in branches:
        if branch["enabled"] == True:
            url = branch["repourl"]
            name = sanitize_repo_name(url)
            current_remotes = git_add_remote(name,url,current_remotes)
   
def setup_linus_branch(manifest,skip_fetch,master_branch):
    #sets up the master branch what we are merging on top of

    main_branch = manifest["master_branch"]
    name = sanitize_repo_name(main_branch["repourl"])
    if master_branch:
        main_branch["use_latest_tag"] = False
    #if use latest_tag is set, get ref of latest tag
    branch_name= main_branch["branch"]
    main_branch[u"tag"] = None
    rev = ""

    if main_branch["use_latest_tag"] == True:
            if skip_fetch == False:
                git_fetch_remote(name)
            #this code is taken out of gen_manifest.sh
            #TODO:find better way than this
            tag_cmd = "git ls-remote --tags {}".format(name)
            tag_cmd+= "| awk '{ print $2 }' | awk -F/ ' { print $3 } ' | sort -Vr | head -1 | sed 's/\^{}//'"
            tag = run_shell_cmd(tag_cmd).split("\n")[0]
            print_and_log("using tag",tag)
            main_branch[u"tag"] =tag
            rev = run_shell_cmd("git rev-list {} -1".format(tag)).split("\n")[0]

    elif main_branch["stuck_at_ref"] != "":
            rev = main_branch["stuck_at_ref"]
    else:
            #-m was passed on comdline do merge off main branch
            #always fetch even if -s was passed
            git_fetch_remote(name)
            fetch_rev = run_shell_cmd("git rev-list {}/{} -1".format(name,main_branch["branch"]))
            rev = fetch_rev.split("\n")[0]
           
    main_branch[u"rev"] = rev
    return manifest

def reset_repo(manifest):
    main_branch = manifest["master_branch"]
    url = main_branch["repourl"]
    name = sanitize_repo_name(url)
    msg = ""
    if main_branch["use_latest_tag"]:
        msg = main_branch["tag"]
    else:
        msg = main_branch["rev"]
    
    print_and_log("Checking out master branch")
    run_shell_cmd("git checkout -f master")
    run_shell_cmd("git reset --hard origin/master")

    print_and_log("Resetting master to {}".format(msg))
    log.write("Resetting master to {}\n".format(msg))
    run_shell_cmd("git reset --hard {}".format(format(main_branch["rev"])))


def do_merge(manifest, continue_merge, branding):
    #main merge function. use git rerere for cached conflicts
    if continue_merge:
        print_and_log(patch_manifest)
        with open(patch_manifest) as f:
            patch_branches = f.read()

    for branch in manifest["topic_branches"]:
        if branch["enabled"] == True:
            if continue_merge and branch["rev"] in patch_branches:
                print_and_log("Skipping {} since patch-manifest says it is merged".format(branch["name"]))
                continue

            print_and_log("Merging {} {} {} {}".format(branch["name"],
                                      branch["repourl"],
                                      branch["branch"],
                                      branch["rev"]))
            try:
                merge_msg= '"{}: Merge commit {} from {} {}\n\n{}"'.format(branding,branch["rev"],branch["repourl"],branch["branch"],gen_manifest_blurb(None,branch,False))
                run_shell_cmd("git merge -m " + merge_msg + " --no-ff  {} --rerere-autoupdate --log" .format(branch["rev"]))
            except Exception as e:
                rerere_output= run_shell_cmd("git rerere status")
                if rerere_output == "":
                    run_shell_cmd("git diff --stat --stat-width=200 --cached")
                    run_shell_cmd("git commit --no-edit")
                    print_and_log("Git rerere handled merge")
                else:
                    print_and_log("Git has has exited with non-zero. check log")
                    raise Exception("Merge has failed. Check git status/merge log to see conflicts/issues. Fix them and then run './merge.py -c (-r)' to continue")
            patch.write("Merging {} {} {} {}\n".format(branch["name"],
                                      branch["repourl"],
                                      branch["branch"],
                                      branch["rev"]))
  
    print_and_log("Merge succeeded")

def print_and_log(*args, sep=" "):
    print(*args, sep=sep)
    print(*args, sep=sep, file=log)


def create_readme_file():
    #Implements the README.intel from the legacy merge.sh
    readme_out = "INTEL CONFIDENTIAL. FOR INTERNAL USE ONLY.\n"
    readme_out += rule+"\n"
    #exlude README.md file
    excludes = ["README.md"]
    files = [x for x in glob.glob("README.*") if x not in excludes]
    for f in files:
        with open(f) as fobj:
            readme_out += f + ":\n\n"
            readme_out += fobj.read()+"\n"

    readme_out += "\n" + rule +"\n"
    readme_out += "Intel Next Maintainers <intel-next-maintainers@eclists.intel.com>"
    with open("README.intel","w") as f:
        f.write(readme_out)

def merge_commit(manifest, project,config_options,branding):
    #If eywa folder has not been merged, print msg and return
    if not os.path.isdir("eywa"):
        print("eywa branch has not been merged - skipping final merge commit")
        return

    artifacts=["manifest","manifest.json"]
    date = datetime.date.today()
    #commit everything important
    for artifact in artifacts:
        run_shell_cmd("mv {} eywa".format(artifact))
        run_shell_cmd("git add eywa/{}".format(artifact))
    
    #Flush log files and add them as well
    #Use cp for log file because it will be stil be written even after we add to git which is a little weird
    log.flush()
    patch.flush()
    os.fsync(log.fileno())
    os.fsync(patch.fileno())
    run_shell_cmd("git add README.intel")
    run_shell_cmd("cp {} eywa".format(log_file_name))
    run_shell_cmd("git add eywa/{}".format(log_file_name))
    run_shell_cmd("cp {} eywa/{}".format(patch_manifest,patch_manifest))
    run_shell_cmd("git add eywa/{}".format(patch_manifest))

    if project != None:
        base_msg = "{} ({} project release): Add release files for ".format(branding,project)
    else:
        base_msg = "{}: Add release files for ".format(branding,project)

    if manifest["master_branch"]["use_latest_tag"]:
        commit_msg = base_msg + "{} {}\n\n".format(manifest["master_branch"]["tag"],date)
    else:
        commit_msg = base_msg + "master:{} {}\n\n".format(manifest["master_branch"]["rev"],date)
    commit_msg += "\nAdded: {} \n\n ".format(", ".join(artifacts+[log_file_name,patch_manifest,"README.intel","README.md"]))

    commit_msg +="\nManifest:\n"
    
    for branch in manifest["topic_branches"]:
        if branch["enabled"]:
            commit_msg +="{} {} {}\n".format(branch["repourl"],branch["branch"],branch["rev"])
    commit_msg +="\n\n"
    
    if config_options:
        commit_msg += "\nConfig Files: \n{} \n\n ".format("\n".join(config_options))
        for config in config_options:
            run_shell_cmd("git add {}".format(config))
    commit_msg +="\n\n"

    # Fast manifest reference added to the default README.md page.
    with open("README.md", "a") as f:
        print("## Fast Manifest Reference\n", file=f)
        print(generate_table(manifest), file=f)
    run_shell_cmd("git add README.md")

    # Kernel versioning - match date based on branding
    localversion = branding.strip()
    localversion = localversion.lower().replace(" ","-")
    with open("localversion-intel", "w") as f:
        print(f"-{localversion}-{datetime.date.today()}", file=f)
    run_shell_cmd("git add localversion-intel")

    run_shell_cmd("git commit -s -m '{}'".format(commit_msg))

def gen_manifest_blurb(project,branch,print_repo_branch):
    f = StringIO()
    contributors = ["{} <{}>".format(contrib["name"],contrib["email"]) for contrib in branch["contributor"]]
    ip_owner = ["{} <{}>".format(contrib["name"],contrib["email"]) for contrib in branch["ip_owner"]]
    sdl_contact = ["{} <{}>".format(contrib["name"],contrib["email"]) for contrib in branch["sdl_contact"]]
    cfg_options = ["#\t{}={}".format(cfg["name"],cfg["value"]) for cfg in branch["config_options"]]

    f.write("#Topic Branch: {}\n".format(branch["name"]))
    f.write("#Classification: {}\n".format(branch["status"]))
    f.write("#Description: {}\n".format(branch["description"]))
    if project != None:
        f.write("#Project: {}\n".format(project))

    if branch["jira"] != "":
        f.write("#Topic Branch JIRA: {}/{}\n".format(BASE_JIRA_URL,branch["jira"]))
    else:
        f.write("#Topic Branch JIRA: N/A\n")

    if "maillist" in branch and branch["maillist"] != "":
        f.write("#Pull Request: {}\n".format(branch["maillist"]))
    else:
        f.write("#Pull Request: N/A\n")

    if "platforms" in branch and branch["platforms"] != "":
        f.write("#Targeted Platforms: {}\n".format(branch["platforms"]))
    else:
        f.write("#Targeted Platforms: N/A\n")

    if "feature_jiras" in branch and branch["feature_jiras"] != "":
        f.write("#Feature JIRAs: \n")
        for jira in branch["feature_jiras"].split(","):
            f.write("#\t{}/{}\n".format(BASE_JIRA_URL,jira))
    else:
        f.write("#Feature JIRAs: N/A\n")

    f.write("#Contributor: {}\n".format(", ".join(contributors)))
    f.write("#Branch Type: {}\n".format(branch["branch_type"]))
    f.write("#Ip Owner: {}\n".format(", ".join(ip_owner)))
    f.write("#SDL Contact: {}\n".format(", ".join(sdl_contact)))
    if cfg_options != []:
        f.write("#Config Options:\n{}\n".format("\n".join(cfg_options)))
    else:
        f.write("#Config Options: N/A\n")

    if print_repo_branch and branch["enabled"]:
        f.write("{} {} {}\n\n".format(branch["repourl"],branch["branch"],branch["rev"]))

    val = f.getvalue()
    f.close()
    return val

def print_manifest_log(manifest,project):
    #Creates TXT version of the manifest which can be sent out
    master= manifest["master_branch"]
    branches = manifest["topic_branches"]
    with open("manifest","w") as f:
        f.write("#Linux upstream\n")
        if master["use_latest_tag"]:
            f.write("{} {} {} {}\n\n".format(master["repourl"],master["tag"],master["branch"],master["rev"]))
        else:
            f.write("{} {} {}\n\n".format(master["repourl"],master["branch"],master["rev"]))

        for branch in filter(lambda x: x['enabled'] == True,branches):
            f.write(gen_manifest_blurb(project,branch,True))

def list_manifest(manifest,list_repos):
    """
        Read in manifest_in and print out the manifest in txt format.

        Args:
        list_repos:(bool) if true print "repourl branch
    """
    git_header = "git remote name/branch"
    if list_repos:
        git_header = "git repo branch"
    headers = ["name",git_header,"enabled"]
    data = []
    for branch in manifest["topic_branches"]:
       repo_branch = "{} {}".format(branch["repourl"],branch["branch"])
       #print git remote name
       if list_repos == False:
           repo_branch = "{}/{}".format(sanitize_repo_name(branch["repourl"]),branch["branch"])
       data.append([branch["name"],repo_branch,str(branch["enabled"])])

    print_and_log(tabulate.tabulate(data,headers,tablefmt="simple"))

def gen_manifest(manifest,skip_fetch,blacklist,whitelist,enable_list):
 
    #If any of the parameters are invalid exit the progran
    for branch_name in blacklist+whitelist+enable_list:
        if branch_name not in [branch["name"] for branch in manifest["topic_branches"]]:
           raise Exception("{} is not a valid branch".format(branch_name))

    #process whitelist
    if whitelist != []:
        topic_branches =[]
        #Do it in this order so we can re-order the manifest based on cmdline
        for name in whitelist:
            for branch in manifest["topic_branches"]:
               if name == branch["name"]:
                   print_and_log("Exclusively enabled {} due to -w option".format(branch["name"]))
                   branch["enabled"] = True
                   #Reset any stuck at ref in manifest_in.json
                   branch["stuck_at_ref"] = ""
                   topic_branches.append(branch)
                   break
        #overwrite the topic branches with our new list
        manifest["topic_branches"] = topic_branches

    #Proccess blacklist
    for branch in manifest["topic_branches"]:
        if branch["name"] in blacklist:
           branch["enabled"] = False
           print_and_log("Disabling {} due to -b option".format(branch["name"]))

    #Process enable_list
    for branch in manifest["topic_branches"]:
        if branch["name"] in enable_list:
           branch["enabled"] = True
           print_and_log("Enabling {} due to -e option".format(branch["name"]))


    #clear out manifest.json to show only enabled branches
    final_topic_branches=[]
    for branch in manifest["topic_branches"]:
        if branch["enabled"] == True:
            final_topic_branches.append(branch)
    manifest["topic_branches"] = final_topic_branches
    
    #Add repos as remotes
    add_remotes(manifest)
    #fetch repos
    manifest=fast_fetch_remotes(manifest, skip_fetch)
    check_manifest_intel_next(create_repo(), manifest)
    return manifest

def parse_config_line(cfg_line):
    """
    Examines a line of text to obtain settings inside a config file.
    Returns a dictionary containing the key-value pair of the setting,
    or an empty dictionary if no setting was found.
    """
    # Special case: line is commented and contains "is not set" means "n".
    re_match = re.match("#(.*) is not set", cfg_line)
    if re_match:
        return {re_match.group(1).strip() : "n"}
    if len(cfg_line.split("=")) == 2:
        name, value = cfg_line.split("=")
        return {name.strip() : value.strip()}
    return {}

def combine_dicts(d1, d2):
    return {**d1, **d2}

def parse_config_file(cfg_file):
    """
    Parses a config file into a dictionary of options and values.
    """
    result_dict = {}
    with open(cfg_file) as f:
        for line in f:
            result_dict = combine_dicts(result_dict, parse_config_line(line))
    return result_dict

def validate_config(cfg_file, all_options, branch_tracker):
    """
    Examines the contents of cfg_file to determine if its options correspond to
    the options provided in all_options.

    Args:
    cfg_file: (path) Path to the config file.
    all_options ([dict(string, string)]) Each dict must contain a name and value.
    branch_tracker (dict(string, list)) Associates a config name to the
    branches that define it.

    Returns:
    A list of dictionaries associating a config name to its expected value,
    value, and branch(es) that defined it
    """
    with open(cfg_file) as f:
        cfg_dict = parse_config_file(cfg_file)
        return [
            {
                "name" : option["name"],
                "expected" : option["value"],
                "value" : cfg_dict.get(option["name"], "Not Found"),
                "branches" : ",".join(branch_tracker[option["name"]])
            }
            for option in all_options
        ]

def filter_table(dct_lst):
    """
    Filters out all entries in the list where expected value is equal to the
    actual value. This is because we don't care about the good values -
    we just want the bad ones.
    """
    return [dct for dct in dct_lst if not dct["expected"].strip() == dct["value"].strip()]

def concat_dict_values(dct_lst):
    """
    Given a list of dictionaries with the same keys, concatenates the values
    into a single dictionary with a list of values.
    """
    result_dict = {}
    for dct in dct_lst:
        for k, v in dct.items():
            result_dict[k] = result_dict.get(k, []) + [v]
    return result_dict

def config_validation(all_options, branch_tracker):
    # Parse all of the config files, and put them into a dictionary.
    cfg_filepaths = [os.path.join(config_path, cfg_file) for cfg_file in config_files_to_val]
    cfg_dictlsts = [validate_config(path, all_options, branch_tracker) for path in cfg_filepaths]

    for filename, dct_lst in zip(config_files_to_val, cfg_dictlsts):
        print_and_log("Filename: {}".format(filename))
        print_and_log(tabulate.tabulate(concat_dict_values(filter_table(dct_lst)), "keys"))
        print_and_log("")

def run_regen_script(all_options,branch_tracker):
    run_shell_cmd("mv {} {}".format(config_fragments, config_path))
    run_shell_cmd_output("./regen_configs.sh")
    output_array = []
    #Do validation to make sure everything was set in Kconfig
    config_validation(all_options,branch_tracker)
    #After validation is complete add files to git
    for config_file in config_files:
        output = run_shell_cmd("git diff {}/{}".format(config_path, config_file))
        lsoutput = run_shell_cmd("git ls-files {}/{}".format(config_path, config_file))
        if (lsoutput == "") or (lsoutput != "" and output != ""):
            output_array.append("{}/{}".format(config_path, config_file))
    return output_array

def is_uppercase(s):
    return s.upper() == s

def pre_kconfig_validation(branches):
    #check for conflicting options and raise exception if found
    configs_dict = {}
    for branch in branches:
        if branch["enabled"] == True and branch['config_options'] != []:
            config_options = branch["config_options"]
            for config in config_options:
                name = config["name"]
                value = config["value"]
                if name in configs_dict and configs_dict[name][0] != value :
                    print_and_log("{} is set to {} and {} by '{}' and '{}'".format(name,value,configs_dict[name][0],branch["name"],configs_dict[name][1]))
                    #TODO: Make a list of conflicts print all of them out and then raise exception
                    raise Exception("Option set by two different branches, resolve the conflict and rerun")
                if value.isupper():
                    print_and_log("{} is set to {}".format(name, value))
                    raise Exception("Option is set to uppercase")
                if not is_uppercase(name):
                    raise Exception("CFG name {} is not all uppercase characters".format(name))
                configs_dict[name] = (value,branch["name"])
 
def gen_config():
    manifest=read_manifest(manifest_out_path)
    branches = manifest["topic_branches"]
    configs_li =[]
    branch_tracker={}
    all_options=[]
    #Create a list of cfg options, and also a dict matching config to branch names
    for branch in branches:
        if branch["enabled"] == True and branch['config_options'] != []:
            config_options = branch["config_options"]
            configs_li.append((" ".join([branch["name"],branch["repourl"],branch["branch"]]),config_options))
            all_options+= branch["config_options"]
            for opt in branch["config_options"]:
                name = opt["name"]
                if name in branch_tracker:
                    branch_tracker[name].append(branch["name"])
                else:
                    branch_tracker[name] = [branch["name"]]

    #Write CFG options to fragment
    with open(config_fragments,"w") as f:
        for cfg in configs_li:
            print_and_log("#Branch: {}".format(cfg[0]))
            f.write("#Branch: {}\n".format(cfg[0]))
            for config in cfg[1]:
                print_and_log("{}={}".format(config["name"],config["value"]))
                f.write("{}={}\n".format(config["name"],config["value"]))
    
    print_and_log("Fragment has been written, running ./regen_configs.sh")
    return run_regen_script(all_options,branch_tracker)

def run_git_describe(manifest):
    for branch in manifest["topic_branches"]:
        if branch["enabled"] == True:
            remote_name = sanitize_repo_name(branch["repourl"])
            tag = run_shell_cmd("git describe {}/{}".format(remote_name,branch["branch"])).split("\n")[0]
            print_and_log("{} {}".format(branch["name"],tag))

def write_manifest_files(manifest,project):
    s=json.dumps(manifest,indent=4)
    open(manifest_out_path,"w").write(s)
    print_manifest_log(manifest,project)

def open_logs(mode):
    if mode not in ["a","w"]:
        raise Exception("mode must be 'a' or 'w'")
    global log
    global patch
    log = open(log_file_name,mode)
    patch = open(patch_manifest,mode)
    args = " ".join(sys.argv)
    log.write(f"Script started with {args}\n")

def branch_is_for_all_subprojects(branch):
    '''Check if branch is to be included in all sub projects'''
    return "project_branches" in branch and branch["project_branches"] == {}

def setup_project_merge(manifest,project):
    '''Modify manifest for project specific merge

       Remove branches that do not have project_branches = {} or
       branches that do not contain the specified sub project
    '''
    del_list = []
    found_project = False
    for branch in manifest["topic_branches"]:
        #If subproject dict exists and the sub project also exists
        if "project_branches" in branch and project in branch["project_branches"]:
            sub_project = branch["project_branches"][project]
            branch["enabled"] = sub_project["enabled"]
            branch["branch"] = sub_project["branch"]
            branch["stuck_at_ref"] = sub_project["stuck_at_ref"]
            #At least one subproject for the passed -p exists
            found_project = True
        #Check if the branch is to added for all subprojects
        elif not branch_is_for_all_subprojects(branch):
            del_list.append(branch)

    if found_project == False:
        raise Exception("Could not find project {} ".format(project))

    #Modify manifest with only branches enabled that are to be merged
    for dbranch in del_list:
        manifest["topic_branches"].remove(dbranch)

def main():
    parser = argparse.ArgumentParser(description=script_name)
    parser.add_argument('-s','--skip_fetch', help='skip git fetch',action='store_true')
    parser.add_argument('-g','--gen_manifest', help='just generate manifest and don\'t merge',action='store_true')
    parser.add_argument('-l','--list_manifest', help='list manifest.in branches with name, git remote and status',action='store_true')
    parser.add_argument('-lr','--list_manifest_repos', help='list manifest.in branches with name,git repos and status',action='store_true')
    parser.add_argument('-c','--continue_merge', help='continue merge using manifest.json/patch_manifest',action='store_true')
    parser.add_argument('-v','--verbose_mode', help='output all git output to terminal',action='store_true')
    parser.add_argument('-m','--master_branch', help='use HEAD of master branch instead of latest tag',action='store_true')
    parser.add_argument('-r','--regen_config', help='regen Kconfig options after merge is completed', action='store_true')
    parser.add_argument('-p','--project_merge', help='do project specific merge',type=str)
    parser.add_argument('-d','--run_describe', help='run git describe on all enabled branches (unless -a is passed )', action='store_true')
    parser.add_argument('-b','--blacklist', help='comma seperated list of branches not to merge even if enabled',type=str)
    parser.add_argument('-e','--enable_list', help='comma seperated list of branches to enable if disabled in manifest_in.json',type=str)
    parser.add_argument('-w','--whitelist', help='comma seperated list of branches to exclusively merge',type=str)
    parser.add_argument('-br','--branding', help=f'Branding for merge default:{DEFAULT_BRANDING}',type=str,default=DEFAULT_BRANDING)
    args = parser.parse_args()

    skip_fetch = args.skip_fetch
    continue_merge = args.continue_merge
    run_gen_manifest = args.gen_manifest
    regen_config = args.regen_config
    run_describe = args.run_describe
    master_branch = args.master_branch
    branding = args.branding

    blacklist = []
    if args.blacklist != None:
        blacklist = args.blacklist.split(",")
    whitelist = []
    if args.whitelist != None:
        whitelist = args.whitelist.split(",")
    enable_list = []
    if args.enable_list != None:
        enable_list = args.enable_list.split(",")

    manifest=read_manifest(manifest_in_path)

    project = args.project_merge
    if project != None:
       setup_project_merge(manifest,project)

    global verbose_mode
    verbose_mode = args.verbose_mode

    if args.list_manifest == True or args.list_manifest_repos:
        list_manifest(manifest,args.list_manifest_repos)
        return

    if continue_merge == False:
        open_logs("w")
        manifest = gen_manifest(manifest,skip_fetch,blacklist,whitelist,enable_list)
        if run_describe:
           run_git_describe(manifest)
           return
        manifest = setup_linus_branch(manifest,skip_fetch,master_branch)
        #write manifest files
        write_manifest_files(manifest,project)
        if run_gen_manifest:
            print_and_log("Manifest generation has completed")
            return
        if regen_config:
           pre_kconfig_validation(manifest["topic_branches"])
        reset_repo(manifest)
    else:
        #Continue merge
        open_logs("a")
        print_and_log("continuing merge with -c option")
        manifest = read_manifest("manifest.json")
        print_manifest_log(manifest,project)
    
    #Do actual merge now that everything is setup
    do_merge(manifest, continue_merge,branding)
    create_readme_file()
    
    config_change = None 
    if regen_config:
        config_change = gen_config()

    #Last step is to do the merge commit which checks in log files/cfg files etc 
    merge_commit(manifest,project,config_change,branding)
    #If we get here without Exception, merge is done
    print_and_log("Merge script has completed without error")


if __name__ == "__main__":
    main()

