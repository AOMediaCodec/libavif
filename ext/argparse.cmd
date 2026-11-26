: # Script to get a local copy of argparse (https://github.com/p-ranav/argparse)
: # needed by some of the command line tools in the apps/ directory e.g. avifgainmaputil.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

: # argparse is a header-only library released under the MIT License.

git clone -b v3.2 --depth 1 https://github.com/p-ranav/argparse.git
