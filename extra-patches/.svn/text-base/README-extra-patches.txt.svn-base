Don't forget the read README-UrT.txt in the base directory.
Among other things, a new server cvar, sv_userinfoFloodProtect, is described
there.

To apply a patch from this directory, perform the following command from
the base directory ioquake3-UrT-server-4.1:

  patch -N -p0 < ./extra-patches/filename.patch


================
block1337.patch:
================

Introduces a new server cvar sv_block1337.  By default it is set to 0, which
makes the server behave exactly the same as without this patch.  When
sv_block1337 is set to a positive value such as 1, clients attempting to
connect that have a qport of 1337 will not be allowed to connect, and will
be given a message "This server is not for wussies".  A known cheat sets
the client qport to 1337.


=================================
callvoteconnectprotect_beta.patch
=================================

Introduces a new server cvar sv_callvoteRequiredConnectTime.  This is the
amount of seconds that a player who just connected must wait before calling
any kind of vote.  The only exception to this is if there are no players
in-game (all other players in spec or no other players on server).  The
default is 0, which is the original behavior.

This patch is still beta so there might be some risk associated with using it.


==================
connectlimit.patch
==================

Introduces two new server cvars: sv_limitConnectPacketsPerIP and
sv_maxClientsPerIP.  If sv_limitConnectPacketsPerIP is set to any value
greater than zero, the server will ignore "connect" packets coming from a
single IP address in excess of four in the past six seconds.  This is a
safeguard against a flood of connect packets from a single IP address source.
The other new server cvar, sv_maxClientsPerIP, can be set to any positive
value to mean that at most that many clients are allowed to be connected
(in-game) from any given IP address.  Both of these new server cvars default
to 0, which means they are disabled.


===================
consoleprompt.patch
===================

Changes the console prompt from "]" to "ioq3ded> ".


===================
cyclemaplimit.patch
===================

Prevents cyclemap vote spam.  A new server cvar sv_callvoteCyclemapWaitTime
controls how many seconds must pass before another cyclemap vote is called
after such a vote fails, regardless of which client is calling the votes.  By
default sv_callvoteCyclemapWaitTime is 0, which results in the original
behavior.  When a map cycles or reloads, the timer is reset and the server
behaves as if a cyclemap vote hasn't been called yet.


====================
forceautojoin.patch:
====================

Introduces a new server cvar sv_forceAutojoin.  By default it is set to 0,
which makes the server behave exactly the same as without this patch.  When
sv_forceAutojoin is set to a positive value such as 1, client commands
"team red" and "team blue" will be translated to "team free".  The former two
commands are sent to the server when the client presses the UI buttons to
join the red or blue team, and "team free" is sent to the server when the
client presses the autojoin button.  So, in essence, this patch forces
players to autojoin.  A message "Forcing autojoin" will be printed to the
user who sent the "team red" or "team blue" command.

Independent of sv_forceAutojoin, this patch also translates the commands
"team r" and "team b" to "team red" and "team blue", respectively.  In the
case where sv_forceAutojoin is enabled, sending these commands allows a client
to bypass the autojoin feature.  Players more familiar with using the
console are thus able to bypass the sv_forceAutojoin feature, which is really
aimed at assisting noobs.


================
forcecvar.patch:
================

Introduces a new rcon command.  Use it like so:

  rcon forcecvar <client> <cvar-key> <cvar-val>

<client> can be a player name or a client number, or "allbots".  Examples:

  rcon forcecvar 12 name n00bsyb00bsy
  rcon forcecvar wTf|crown name TehNoobPwner
  rcon forcecvar allbots funred capgn,shades,ponygn
  rcon forcecvar allbots funblue capyw,shades,ponygd
  rcon forcecvar allbots racered 1
  rcon forcecvar allbots raceblue 1
  rcon forcecvar allbots cg_rgb "240 108 146"

You can also delete a cvar by omitting the last argument like so:

  rcon forcecvar Rambetter name ""
  rcon forcecvar Rambetter name


===========================
freebsd-prescott32bit.patch
===========================

For 32 bit FreeBSD systems; enables a compile optimization which produces
a binary that can only run on Prescott architectures (or newer).  The patch
is harmless for other operating systems.


==========
goto.patch
==========

Disclaimer: Rambetter hates this feature.  A lot of admins have asked for this
patch so I felt pressured to include it.

Introduces 4 new server cvars.  You can add this to your server.cfg:

  set sv_allowGoto "1" // default 0, don't allow client "goto" command
  set sv_gotoWaitTime "180" // secs between client "goto" commands
  set sv_allowLoadPosition "1" // default 0, don't allow "loadposition"
  set sv_loadPositionWaitTime "180" // secs between "loadposition" commands

These are commands that clients can type to use this new functionality:

  helpgoto
  saveposition
  loadposition
  allowgoto 1
  allowgoto 0
  goto <client>

Saved positions are not persistent across map changes and/or reconnects.


===============
incognito.patch
===============

Introduces a new rcon command.  Use it like so:

  rcon incognito

This will place your client into stealth mode.  Everybody in the server will
temporarily think that you've disconnected, but in reality you will be in
spectators.  Note that this cannot be used to cheat during matches because
the player that is incognito will still show up in a "/serverstatus".

Introduces another new rcon command.  Use it like so:

  rcon sendclientcommand <client> <command>...

<client> can be a player name or a client number, or "all" or "allbots".
Example:

  rcon sendclientcommand Rambetter cp "Hi, this is some big text"


=============
ip2loc.patch:
=============

Enables messages that look like the following when players connect:

  Rambetter connected
      from SAN DIEGO, CALIFORNIA (UNITED STATES)

In addition, players will have the location stored in the new "location"
cvar in their userinfo string.

To have this patch run correctly, you have to point your game server to
a valid ip2loc server.  Information on the ip2loc server can be found here:
http://daffy.nerius.com/ip2loc/

Here is a sample of lines you'll want to add to your game server's server.cfg:

  set sv_ip2locEnable "1" // default 0, don't enable
  set sv_ip2locHost "localhost:10020" // host and port for the ip2loc service
  set sv_ip2locPassword "pa55w0rd" // password for the ip2loc service

This patch also defines a new connectionless packet handler for a command
"getstatuswloc".  This command is similar to "getstatus" except that player
locations are added to the player listing.


========================
loadonlyneededpaks.patch
========================

Introduces some nice features related to map loading.

First, map names are auto-corrected with respect to capitalization.  This is
especially useful on third party map servers where map voting is enabled.
Let's say there exists a map pak file called ut4_icyjumps.pk3 containing
maps/ut4_icyjumps.bsp.  Someone calls a vote by typing
"/callvote map Ut4_IcyJumps" in the console.  The vote passes.  The old
behavior was that the map maps/Ut4_IcyJumps.bsp would be loaded (it worked)
and then clients that didn't have the map would attempt to download the file
Ut4_IcyJumps.pk3, which is spelled incorrectly and can't be downloaded.  So
anyone not having the map would be shut out from the server.  The new
functionality added with this patch is that the names of maps are
auto-corrected with respect to capitalization in a very intelligent way.
Also, "/callvote nextmap ut4_mapname" votes are fixed such that g_NextMap
isn't set unless ut4_mapname is a valid map, and the value that is assigned
to g_NextMap has its capitalization fixed.  Note however that direct admin
calls (such as console commands or rcon) that modify g_NextMap do not
undergo this check and correction.

Second, minor improvements in the error reporting when a map cannot be
loaded.  There were some cases where an "/rcon map xxxx" in the console
would have loaded a map when it shouldn't have, in particular when the
bsp was not in a pk3 and the server was on sv_pure 1.  The logic for
correcting map loading and error reporting also applies to maps being
loaded via mapcycle.txt, g_NextMap, and voting (map, nextmap, and cyclemap).

Third, a minor change related to file search path.  With vanilla ioq3ded-UrT,
the base game and base mod directories in fs_homepath are added to the search
path.  With this patch, the only fs_homepath directory added to the search
path is the one for the top-level mod, in this case q3ut4.  This top-level
fs_homepath directory is the "third party map directory" and will be
discussed below.  On UNIX systems, this directory is usually ~/.q3a/q3ut4/.
On all systems it is the directory where q3config*.cfg is saved.  fs_homepath
can be overridden by using "+set fs_homepath xxxx" on the command that
launches ioq3ded-UrT.  For purposes of the discussion below, the term
"third party map" shall mean any pk3 file in the third party map directory
whose name does not begin with a 'z' or a 'Z'.

Fourth, introduces a new server cvar sv_loadOnlyNeededPaks.  When this is
set to a nonzero positive value, the server will only reference a single
third party map: the one that is being loaded.  All other third party maps
won't be loaded.  This has three big side effects on clients:

  - The third party map will be running in a "sandbox" and won't be affected
    by resources such as textures and shaders from any other third party maps.
    The behavior will be as if though the third party map currently loaded is
    the only third party map installed on the server.  Some nasty issues such
    as missing or incorrect textures might be thus avoided; such issues are
    caused by incorrectly made maps.  One incorrectly made map can affect
    another correctly made map, unfortunately.

  - The complete list of third party maps on the server won't show up in the
    client's callvote menu.  The client will have to manually type
    "/callvote map xxxx" to request a third party map.

  - A long list of loaded third party maps won't overflow a buffer on the
    client, so the client won't get the infamous
    "CL_ParseGamestate: bad command byte" error when connecting to the server.

sv_loadOnlyNeededPaks should therefore be set to 1 on jump servers that have
many and/or poorly made maps.  By default, sv_loadOnlyNeededPaks is set to 0,
which causes all paks to be loaded.  Note that this (meaning loading all pak
files) is the way ioquake3 was meant to work, but Urban Terror decided to
make things work slightly differently by assuming that everything needed to
load a third party map is in a single file <mapname>.pk3.

if you are putting pk3 files into your third party map directory that need
to be loaded on every map (for example containing bot definitions or shader
fixes for base maps), you should make sure that the pk3 files start with the
letter 'z' so that they are always loaded.

Rambetter's next project in the near term is to add functionality to the
server enabling clients to request a complete list of third party maps on the
server.


==================
logcallvote.patch:
==================

Console logging for callvotes from players.  This is an example of what you'll
see in your console:

  Callvote from Ramb (client #0, 160.33.43.5): kick Longbeachbean


==================
logrconargs.patch:
==================

Introduces a new server cvar sv_logRconArgs.  By default it is set to 0,
which makes the server behave exactly the same as without this patch.  When
sv_logRconArgs is set to a positive value such as 1, the logging format
of successful and unsuccessful rcon attempts is changed (improved, I hope).
The new rcon logging will look like this:

  Bad rcon from 160.33.43.5
  Rcon from 160.33.43.5: kick Longbeachbean


=============
mutefix.patch
=============

Fixes the admin mute functionality by not allowing any chat, radio, or
callvotes from clients who are muted.  The original mute functionality only
disallowed some forms of chat; it still allowed radio and callvote.  This
patch doesn't even allow that.  Note that all clients are automatically
unmuted during a map cycle or reload in order to preserve the original
behavior in that respect.


==================
namesanitize.patch
==================

Introduces a new init server cvar sv_sanitizeNames.  When set to 1 (default 0),
modifies the player names in the ioquake3 server code to resemble the
sanitized names in the QVM server code.  The player userinfo strings are
affected.  For example, white space is stripped and colors are removed.
The empty name gets converted to "UnnamedPlayer".  Because this is a CVAR_INIT,
you need to enable it by adding "+set sv_sanitizeNames 1" to your ioq3ded-UrT
command.


==============
nokevlar.patch
==============

Mostly for jump servers, introduces a new init server cvar sv_noKevlar.  By
default it is set to 0, which makes the server behave exactly the same as
without this patch.  When sv_noKevlar is set to a positive value such as 1,
kevlar will be removed from all players entering the game.  In addition, the
server will try to give players a medkit if it's possible to do so.  Because
this is a CVAR_INIT, you need to enable it by adding "+set sv_noKevlar 1" to
your ioq3ded-UrT command.


============
novoip.patch
============

Adds "USE_VOIP = 0" to Makefile, disabling VoIP code completely.


===============
playerdb.patch:
===============

Adds support for an external ban system and player database.  Full
documentation for this system:  http://daffy.nerius.com/bansystem/
In short, here are things you should add to your game server's server.cfg
file:

  set sv_requireValidGuid "1" // default 0, don't require well-formed cl_guid
  set sv_playerDBHost "localhost:10030" // host and port of player database
  set sv_playerDBPassword "s3cr3t" // password for player database
  set sv_playerDBUserInfo "1" // default 0, don't send userinfo to database
  set sv_playerDBBanIDs "cheaters,asshats" // banlists in use, default none
  set sv_permaBanBypass "pa55" // default none

The last cvar, sv_permaBanBypass, can be set to a non-empty string to allow
players who are banned to enter the server.  This is essentially a password
to bypass the ban system completely.  The client would use this command:

  setu permabanbypass "pa55"


==================
radiodisable.patch
==================

Introduces a new server cvar, sv_disableRadio.  When it's set to a nonzero
positive value, all radio is disabled on the server.  The default value for
this cvar is 0, which is normal behavior.


===================
reconnectwait.patch
===================

Introduces a new server cvar, sv_reconnectWaitTime.  When it's set to a
nonzero positive value, clients must wait that many seconds before
reconnecting to a server [after leaving for whatever reason].  Clients will
get a message "Reconnecting, please wait..." on the loading screen while
they're waiting to reconnect.  If a client was kicked due to admin action
or due to a vote to kick, they will have to wait twice as long before
reconnecting.  If a client was kicked due to team killing, they will have to
wait three times as long before reconnecting.

By default, sv_reconnectWaitTime is 0, which means clients don't have to
wait at all when they reconnect to a server.  This is the standard behavior
with or without this patch.


====================
specchatglobal.patch
====================

Introduces sv_specChatGlobal.  When this is set to a positive value, all spec
chat will be broadcast to all players, not just spectators.  By default this
cvar is 0, which leaves the behavior of spec chat unchanged.
