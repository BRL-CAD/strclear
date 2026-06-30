strclear is a basic utility with two main jobs - clearing strings in binary
files, and replacing them in text files.  It is used as part of the bundling
process with BRL-CAD, to help make sure we don't have stray build-time paths
accidentally resulting in programs working when they shouldn't by referencing
build-directory resources.

dirsync is a utility to handle copying a bext install output directory's contents
into a BRL-CAD build directory for staging, and handle subsequent updating of
the build dir contents if the bext install contents should change.  One of
the dirsync options generates a list of changed inputs that is suitable to be
fed into strclear.

TODO - either teach plief to take such a list, or have strclear also support
the set-rpath option - leaning towards the latter, since plief gets used in the
install step that integrates with CMake's install logic and that's not a list
processing scenario the way it's currently handled.  Moreover, the rpath
handling has to be done BEFORE the standard strclear ops to make sure we don't
blow away path info we need to set up new RPATHS, so adding a set-rpath option
to strclear would let strclear itself ensure things are done in the right
order - the CMake side would literally simplify to run dirsync and feed its
list with the path info/options to strclear.

Possible drawback is strclear would need to depend on LIEF for the necessary
capabilities like plief does, but that shouldn't be too big a deal either
way...  there isn't really another candidate on the horizon to replace LIEF
at the moment, and even if one does come along it's not a terribly complex
setup.  The LIEF upstream doesn't see interested in plief (I think they
went with a tool implemented in a language we don't use...) so might be better
just to fold the bits we need into strclear and let do the Right Thing without
having to orchestrate elsewhere.  Have to double check how that would play
with the final post-install update setup, but it ought to simplify something
to not have three tools to juggle...  In fact, that gives us a separation of
concerns.  dirsync syncs files and handles symlinks and other filesystem
issues, while strclear mods files removing path strings and dealing with RPATH.
