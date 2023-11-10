# SVOS Next Markdown Documentation Topics

The doc folder contains Markdown documentation (.md format) for various changes
people have made to the base kernel for the sake of SVOS and system validation.

It is hard to track these changes as overall capabilities. The original
developers move on or just lose track of what they did. The code gets
manipulated by other people as separate chunks. Worse still, we sometimes lose
all authorship history during the release processes (this should be avoidable
now but the final workaround loses the information, and some developers insist
on using the destructive process).

This directory with its documentation is a check against this entropy by
providing a separate place to list information about what was changed, how it
was changed, the purpose of the changes, and the motivation for them. For this
to work, developers must write these files, and we intend to block kernel
changes from both the core team and larger validation if some documentation is
not included.

# How to Document New Changes

There is a template in template.md you can consider for writing your
documentation. This standard is not rigid and it's possible your documentation
will be rejected despite using the template if it doesn't convey essential
types of information outside the scope of the template. The general goal is to
be able to grep for file names, user names, email addresses, key words, 
constants, functions, and any other distinguishing things that somebody could
use in retrospect to find the motivation for a feature and who was involved.

We are working on a BKM to profile the effect of the change on the kernel.
The template has the current details. Generally, we want to know the change
in size to the kernel files and overall size of modules. Then we try to
assess the effect on memory of the kernel, and any possible known issues
with kernel speed/overhead.
