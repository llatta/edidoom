# Edidoom
*Intel Edison based video game console playing Doom*

For information on the project and how to build the hardware to run this, visit http://2ld.de/edidoom/

## Build instructions

This directory contains an Eclipse project that can be imported into Eclipse via the menu option 
*File/Import/General/Existing Projects into Workspace*.

For the full build environment for the Intel Edison, use the Eclipse version from the
[Intel's IoT Developer Kit 1.1](https://software.intel.com/en-us/iot)

After import, create a new Debug configuration for the project using the Edison connection
you've set up in the Remote System Explorer and run.

You will need a WAD file (the game data) from Doom to run it. One option is to download the shareware version of Doom
from ftp://ftp.idsoftware.com/idstuff/doom/doom19s.zip.
Then run the installer in DOS, eg via [DOSBox](http://www.dosbox.com/). 
Copy the resulting `doom1.wad` file to the Edison.
Doom searches the home directory `/home/root` for WAD files, so just put it there.
Make sure the filename is all lower case, it might be upper case when copying from DOS.

## Further information

Visit the project website at http://2ld.de/edidoom/ for more details.

Enjoy!
