# SVOS Next Kernel
SVOS uses the Intel Next project as its base kernel on to which it has applied all the necessary changes so far for system validation.

## The doc Folder
Underneath the root folder is a doc folder containing information about specific changes applied to the kernel for the sake of SVOS. We intend to maintain this over time _and require changes there when new
modifications are made._

## Current Project Management
There is no dedicated resource for maintaining the SVOS Next kernel any more. Kernel releases and support are rotated across members of the Global OS AN team, particularly:
* Adam Preble
* Eric Fagerburg
* Adam Caldwell

Our main responsibility currently is maintaining the SVOS changes against the Intel Next kernel during each kernel release.

New feature development will likely be ZBB'd. New Intel features and issues with Intel features should go through channels associated with Intel Next first and foremost. The SVOS core team does not implement
and integrate other driver code and kernel features; they come from Intel Next.

## Making Changes

We will still encourage pull requests or emailing patches to bring new changes in. There are a few things to consider:
1. Submitted code should follow the Linux Kernel style guidelines "as much as practically possible":
   https://www.kernel.org/doc/html/v4.10/process/coding-style.html
   The document is written with Linus Torvald's classic charisma and we don't think you're stupid if you don't think something should be obvious that he says is obvious.
2. The changes should be run against the current active kernel versions and the pre-release kernel.
3. The changes should be checked in the continuous regression.


### Required Added Documentation
Major changes need to be explained in the doc folder in a Markdown file. This will help us make sure we understand the context of the work years from now. There is a doc/README.md file
and a doc/template.md file to help in doing this. The kernel source gets munged by build system tools and its history can get distorted, so we can't rely on the classic thing of annotating
source, finding the author, and the commit of the change to try to shake out intent and the scope of a change. This documentation gives us a major recourse for figuring out all this
information later.

### Changing Kernel CONFIG_* Configuration Constants
If you intend to change kernel CONFIG constants then reference these files in debian/configs:
* debian/configs/svos_next_defconfig.fix
   * Default, common kernel configuration applied to all combinations of builds. _You probably want this._
* debian/configs/svos-next-default_x86-64.cf
   * Settings to add to the default builds. They are the ones that end with the `default` suffix. You probably __don't__ want to touch this! Even if you are experimenting with just the default
     configuration, use svos_next_defconfig.fix.
* debian/configs/svos-next-tickless_x86-64.cf.
   * These settings live on top of svos_next_defconfig.fix and provide the tickless kernel combinations.
   * Only use if you are affecting tickless kernels.
* debian/configs/svos-next-docker_x86-64.cf.  These settings live on top of svos_next_defconfig.fix and provide the docker kernel combinations.
   * These settings live on top of svos_next_defconfig.fix and provide the docker-specific kernel combinations.
   * Only use if you are affecting docker kernels.
* debian/configs/svos-next-rasdefault_x86-64.cf.  These settings live on top of svos_next_defconfig.fix and provide the rasdefault kernel combinations.
   * These settings live on top of svos_next_defconfig.fix and provide the rasdefault kernel combinations.
   * Only use if you are affecting rasdefault kernels.
* debian/configs/intel_next_defconfig.fix and debian/configs/intel-next-generic_x86-64.cf
   * Do not touch. These configurations are used by DCG for special Intel Next builds that happen to use the SVOS Autobuilder to get updates.

After you build the kernel, you should do a little sanity check and see what happened to those flags. If you look in the debian folder, you can see output folders for the package files and
where they go on disk. For example, you may see debian/svos-next-6.2.0-x86-64-svos-next-default if you built 6.2.0. Inside you will find debian/svos-next-6.2.0-x86-64-svos-next-default/boot/config-6.2.0.svos-next-default-x86-64. This is the file showing all the configuration settings used to make the kernel as a use would find them in the /boot partition of their system after
installing it. You should look at your flags and see if something changed them underneath you. This can happen if you didn't fully satisfy the prerequisites for turning something on, something
overrode the setting otherwise, or you didn't apply it in the right place.
   
## Manually Building the Kernel

These steps completely ignore the changes you want to make and just explain how to get a kernel build. The important thing, however, is the make sure you're working on a branch that actually has the
kernel code! The `master` branch does not! Here is a convention:

1. svos-next: This has the latest changes. All new work has to ultimately go here too. The pre-release kernel is built from this.
2. svos-next-X.Y where X.Y is the kernel version of interest. Changes here will ultimately need to work with the svos-next branch too.
3. sv/ABCYYWW-svos-next-X.Y (example: sv/spr2218-svos-next-5.17). This is a project-specific kernel. Changes here only need to be carried into svos-next if they intend to be permanent.

### Prerequisites

You need some kind of SVOS build environment such as:
1. An actual SVOS platform. This should have a good amount of userspace RAM allocated. A server platform with 8GB of RAM will probably run out of RAM during the build because `make` will drag all the cores
   into the build and eat up your heap. You can try to reduce this by restricting the cores the make command uses but that's out of scope here.
2. An SVOS virtual machine. There are base images that will boot in a VM--at least on QEMU on Linux. Some of the project-specific ones also boot in QEMU but it's a bit of a quirk.
3. An SVOS chroot or schroot. This is an advanced topic and we assume you know what you're doing if you have one.

__The hardware should be fast__. With a CFL workstation with 32GB of RAM, the complete build takes over an hour unattended. That's 12 cores pounding away at it.

You will need these packages installed:
1. Definite and probably a permanent dependency: `dpkg-dev`. This will give you `dpkg-buildpackage`, which in turn will tell you what else you're really missing when trying to build the package.
2. Kernel dependencies listed in debian/control: debhelper, gcc, bc, openssl, libssl-dev, libelf-dev, libdwarf-dev, libunwind-dev, libpci-dev, libpopt-dev, linux-libc-dev
   * Note this may change and become stale. When you actually run the build command, it'll tell you if you're missing something else.
   
### Actually Building the Kernel

1. Take note of the directory above where you have the checkout of the repository because all the .deb files are going to wind up there by default!
2. Make sure you're on a branch where the kernel source actually exists! By default, use svos-next.
3. cd into your svos-next project directory and make sure you are in the root of it.
4. Execute `dpkg-buildpackage -uc -us`.
5. Wait.
6. Keep waiting. Get coffee, mow the lawn, take a stroll, take a nap . . .
7. Assuming you don't see an error at the end, you should have .deb files in the directory above that are good to go.

### Building the Kernel Faster
What's slower than building the kernel? It's building the kernel three times! You can shut some of them off. In debian/rules, you'll find the three build configurations:
```
ifeq ('$(GNUARCH)','x86_64')
DEFARCH:=x86-64
$(eval $(call kernel_build_config,$(KVERSBASE),x86_64,svos-next-default,x86-64,10,svos-next))
$(eval $(call kernel_build_config,$(KVERSBASE),x86_64,svos-next-tickless,x86-64,20,svos-next))
$(eval $(call kernel_build_config,$(KVERSBASE),x86_64,intel-next-generic,x86-64,20,intel-next))
endif
```
If you have a particular configuration of interest, you can comment out the others _except_ that the tickless configuration depends on build output from the default configuration. You need to build default to build tickless.
We think this is strange and have a longstanding open to eventually figure it out.

We've also noted some goofiness building modules unless the intel-next configuration was built, but this might be a fluke and is going to be investigated on-and-off as we try other special module builds for whatever situation.

### Interactive Development
If you're really hacking away, you can just run `dpkg-buildpackage` once to set up build directories under the debian folder. You can then drop into them and try to use `make` like you were
working with vanilla kernel source. They should _mostly_ resume from changes and just build off of what you changed. However, you want to run `dpkg-buildpackage` off of the root one more time
afterwards to be 100% certain everything properly builds.

### Testing and Deploying the Kernel
A pile of .deb files will appear one directory above the git repository's path where you started the build if nothing went wrong. You really only need one of these files just for booting and testing the kernel. The ones that matter, depending on kernel version and the Debian build version numbers, will be something like:

`svos-next-X.Y.0-x86-64-svos-next-[default or tickless]_X.Y.0+#####_amd64.deb`

Where:
* X.Y is the kernel version
* ##### is the build version from the changelog file in debian/changelog
* \[default or tickless\] is the build configuration you care to use.

For example: svos-next-6.3.0-x86-64-svos-next-tickless_6.3.0+26317_amd64.deb
* X.Y is 6.3
* ##### is 26317
* it is a tickless kernel

Upload that package to whatever machine or VM, and install it with:

`dpkg -i svos-next-X.Y.0-x86-64-svos-next-[default or tickless]_X.Y.0+#####_amd64.deb`

This will add it to GRUB and you can pick it on the boot menu. But if you just can't get the timing right, you can tell GRUB to target it automatically. Assuming that 6.3 tickless kernel, you would use:
`grub-reboot 'SVOS-NEXT_6.3.0_svos-next-tickless_default'`

Chances are you don't magically know what phrase to put there. The easy thing to do is dump them by grepping them out of the GRUB configuration:
`grep menuentry /boot/grub/grub.cfg`

The grub-reboot command will target that kernel just for the next reboot. If it turns out to be a dead kernel, it won't keep trying to boot back into it and instead revert to whatever your previous default happened to be.
