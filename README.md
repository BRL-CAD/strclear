strclear is a basic utility with two main jobs - clearing strings in binary
files, and replacing them in text files.  It is used as part of the bundling
process with BRL-CAD, to help make sure we don't have stray build-time paths
accidentally resulting in programs working when they shouldn't by referencing
build-directory resources.

