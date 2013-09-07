TERMINOLOGY
===========

ioq3ded-UrT  = this project, an improved replacement for ioUrTded
ioUrTded     = the original UrT server binary for Urban Terror 4.1
               (another project, not this one)


DETAILS
=======

The code in this project can be compiled to generate a server binary
suitable for running Urban Terror 4.1 servers.  This code originally
started out as the plain vanilla release of ioquake3 1.36 (from
svn://svn.icculus.org/quake3/tags/1.36/).  The original 1.36 code has
been modified to include exploit fixes specific to Urban Terror 4.1.
Other enhancements and miscellaneous bug fixes have been made as well.

This code generates a server binary called ioq3ded-UrT.  This is a
drop-in replacement for the original ioUrTded from Urban Terror 4.1.
There are a few differences; see the "DIFFERENCES FROM ioUrTded"
section below.

ioq3ded-UrT is preferred over the original ioUrTded because it's
more stable and bug-free (in my experience).  For example, using
ioq3ded-UrT to run a full server with large and complex jump maps
seems to cause less stability issues.


BEFORE YOU COMPILE
==================

Please read the file extra-patches/README-extra-patches.txt.  You may
decide to use extra functionality provided by patches described there.


COMPILING
=========

The procedure for compiling ioq3ded-UrT is the standard method, and
is described in the README file.  (Note: The README file is the original
one that comes with plain vanilla ioquake3.)  There is no need to set
any additional Makefile parameters; they are already set to correct
values for compiling a server binary.


DIFFERENCES FROM ioUrTded
=========================

This section contains an [incomplete?] list things that make ioq3ded-UrT
different from the original ioUrTded.  For the most part, ioq3ded-UrT
is a drop-in replacement for ioUrTded.

- I'm pretty sure that ioq3ded-UrT is incompatibe with Quake III Arena
clients, but I'm not positive because I don't own a copy of Quake III Arena
so I cannot test this.  The original ioUrTded did some very sketchy things
in order to trick Q3A clients into thinking that they were connecting to a Q3A
server with pak0.pk3 from Q3A present.  This, in my opinion, is a total hack
and I'd rather not add that logic to ioq3ded-UrT at the expense of not being
compatible with Q3A.  If I happen to stumble upon a copy of Q3A or if someone
who has it installed can help me test, I may be convinced to write a patch
(for extra-patches/) that includes this logic to help Q3A clients connect.
Note also that it's possible to add Q3A's pak0.pk3 into baseq3/ (which sits
alongside q3ut4/) on the server if you want to make Q3a clients happy, but
then you might cause other issues for some clients (because you'll be
reporting pak0.pk3 in the server's pak list, which clients might not have).
The compatibility issues between ioUrbanTerror, ioquake3 vanilla, and
Q3A are very delicate and fragile.

- A new server cvar has been added: sv_userinfoFloodProtect.  Make sure
it's set to a nonzero value to protect against very quickly changing
userinfos.  It will limit the number of userinfo changes in the previous
2 seconds for each client to 2.  By default this cvar is set to 1
(enable).  In ioUrTded, sv_floodProtect controls spamming userinfo
changes, but in ioq3ded-UrT sv_floodProtect only limits client commands,
not userinfos.

- The compiled binary will be called ioq3ded-UrT.xxx instead of
ioUrTded.xxx.  You can rename it to whatever you like.

- ioq3ded-UrT creates a config "cache" file named q3config_server.cfg,
whereas the analagous file in ioUrTded is called q3config.cfg.

- The original ioUrTded does some very quirky (and in my opinion incorrect)
things related to reporting loaded pak files to the client.  On the other
hand, ioq3ded-UrT adheres to the ioquake3 standard way of loading pak files -
that is, all pak files are loaded and they are all reported to the client as
being loaded.  Unfortunately, there are two things wrong with this standard
approach:
  * Most Urban Terror clients assume that everything needed to load a third
    party map is contained in a single pak file named <mapname>.pk3, and
    these clients will only attempt to download that single file.
    There might be inconsistency between loaded paks on the server and
    loaded paks on the client, which may cause random missing/incorrect
    texture issues if there are poorly made maps that conflict with each
    other on the server.
  * Servers with very many third party maps installed will report all
    installed third party maps to the client, and this may trigger some kind
    of client-side overflow which causes the infamous
    "CL_ParseGamestate: bad command byte" error.
To overcome these issues, you should apply the patch
extra-patches/loadonlyneededpaks.patch and use the server cvar
sv_loadOnlyNeededPaks that is defined there.  More information about this
patch is in extra-patches/README-extra-patches.txt.


DIFFERENCES FROM ioquake3-1.36
==============================

Revision 14 of this SVN repository contains the exact code that is the
ioquake3 1.36 release.  Therefore, to see all code differences from the
original 1.36, do something like:

  svn diff -r14

from the base project directory.
