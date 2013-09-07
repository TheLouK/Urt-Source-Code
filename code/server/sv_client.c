/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// sv_client.c -- server code for dealing with clients

#include "server.h"

static void SV_CloseDownload( client_t *cl );

/*
=================
SV_GetChallenge

A "getchallenge" OOB command has been received
Returns a challenge number that can be used
in a subsequent connectResponse command.
We do this to prevent denial of service attacks that
flood the server with invalid connection IPs.  With a
challenge, they must give a valid IP address.

If we are authorizing, a challenge request will cause a packet
to be sent to the authorize server.

When an authorizeip is returned, a challenge response will be
sent to that ip.
=================
*/
void SV_GetChallenge( netadr_t from ) {
	int		i;
	int		oldest;
	int		oldestTime;
	challenge_t	*challenge;

	// ignore if we are in single player
	if ( Cvar_VariableValue( "g_gametype" ) == GT_SINGLE_PLAYER || Cvar_VariableValue("ui_singlePlayerActive")) {
		return;
	}

	oldest = 0;
	oldestTime = 0x7fffffff;

	// see if we already have a challenge for this ip
	challenge = &svs.challenges[0];
	for (i = 0 ; i < MAX_CHALLENGES ; i++, challenge++) {
		if ( !challenge->connected && NET_CompareAdr( from, challenge->adr ) ) {
			break;
		}
		if ( challenge->time < oldestTime ) {
			oldestTime = challenge->time;
			oldest = i;
		}
	}

	if (i == MAX_CHALLENGES) {
		// this is the first time this client has asked for a challenge
		challenge = &svs.challenges[oldest];

		challenge->adr = from;
		challenge->challenge = ( (rand() << 16) ^ rand() ) ^ svs.time;
		while (challenge->challenge == 0) {
			// I don't like it when the challenge is zero.  In practice, everything should
			// work just fine when the challenge is zero; however, I think it's more correct
			// when it's not allowed to be zero for at least the following reason.  When
			// we get the userinfo from the client, we parse the challenge using atoi().
			// When the challenge is missing or is not parsed successfully, atoi() returns zero.
			// This just makes me feel uncomfortable.  Better to not allow zero challenge so
			// that zero can take on a special meaning which means "uninitialized".
			challenge->challenge = ((rand() << 16) ^ rand()) ^ svs.time;
		}
		// Can leave challengePing uninitialized, it's initialized in SV_DirectConnect, based on connected state.
		challenge->time = svs.time;
		challenge->pingTime = -1;
		challenge->connected = qfalse;
		challenge->authChallengesSent = 0;
		challenge->permabanned = qfalse;
		i = oldest;
	}

	// We cannot have the classic Q3 authorize server and our custom playerdb/auth server
	// both dictate which players are allowed in.  We have to choose one system or the other.
	// If the playerdb host is specified and if ban IDs are specified, we will choose the custom
	// playerdb auth server, even if resolution of the playerdb host was unsuccessful.
	if (sv_playerDBHost->string[0] && sv_playerDBBanIDs->string[0]) {
		SV_ResolvePlayerDB();
		qboolean chalAuthTimedOut = qfalse;
		// If the challenge is already marked as permabanned, we've already sent a challengeResponse
		// to the client.  So they may be asking for it again because they didn't receive it, or it's
		// just bad timing.
		if (challenge->permabanned || svs.playerDatabaseAddress.type == NA_BAD || Sys_IsLANAddress(from) ||
				(!(from.type == NA_IP /* || from.type == NA_IP6 */)) || // Not handling IPv6 yet.
				(chalAuthTimedOut = (svs.time - challenge->time > PLAYERDB_CHALLENGEAUTH_TIMEOUT))) {
			if (chalAuthTimedOut) {
				Com_DPrintf("Challenge auth timed out\n");
			}
			// Let them in immediately, even if they are banned.  We want their userinfo string
			// before we drop them.  Also there is a chance for them to bypass the ban with a correct
			// "permabanbypass" in their userinfo string.
			challenge->pingTime = svs.time;
			NET_OutOfBandPrint( NS_SERVER, from, "challengeResponse %i", challenge->challenge );
		}
		else if (challenge->authChallengesSent < 2) { // Never send more than 2 auths per challenge.
			// Note: There is still a possibility of flood attacks here.  If a client sends thousands of
			// getchallenge packets, each with a different source port, this will cause that many auths
			// to be sent.  Note that it's easy to forge the source address on a UDP packet.
			Com_DPrintf("Sending authorizePlayer packet to playerdb for client address %s\n",
				NET_AdrToString(from));
			NET_OutOfBandPrint(NS_SERVER,
				svs.playerDatabaseAddress,
				"playerDBRequest\n%s\nauthorizePlayer:%08x\n%s\n%s\n",
				svs.playerDatabasePassword,
				0x7fffffff & (challenge->challenge),
				sv_playerDBBanIDs->string,
				NET_AdrToString(from));
			challenge->authChallengesSent++;
		}
		return;
	}
	// Otherwise the playerdb stuff isn't enabled, so we should fall through to use
	// the legacy code.

#ifdef STANDALONE
	if (challenge->pingTime == -1) { challenge->pingTime = svs.time; }
	NET_OutOfBandPrint( NS_SERVER, from, "challengeResponse %i", challenge->challenge );
#else
	if (Cvar_VariableIntegerValue("com_standalone") || Sys_IsLANAddress(from)) {
		if (challenge->pingTime == -1) { challenge->pingTime = svs.time; }
		NET_OutOfBandPrint(NS_SERVER, from, "challengeResponse %i", challenge->challenge);
		return;
	}

	// Drop the authorize stuff if this client is coming in via v6 as the auth server does not support ipv6.
	if(challenge->adr.type == NA_IP)
	{
		// look up the authorize server's IP
		if ( !svs.authorizeAddress.ip[0] && svs.authorizeAddress.type != NA_BAD ) {
			Com_Printf( "Resolving %s\n", AUTHORIZE_SERVER_NAME );
			if ( !NET_StringToAdr( AUTHORIZE_SERVER_NAME, &svs.authorizeAddress, NA_IP ) ) {
				Com_Printf( "Couldn't resolve address\n" );
				return;
			}
			svs.authorizeAddress.port = BigShort( PORT_AUTHORIZE );
			Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", AUTHORIZE_SERVER_NAME,
				svs.authorizeAddress.ip[0], svs.authorizeAddress.ip[1],
				svs.authorizeAddress.ip[2], svs.authorizeAddress.ip[3],
				BigShort( svs.authorizeAddress.port ) );
		}

		// if they have been challenging for a long time and we
		// haven't heard anything from the authorize server, go ahead and
		// let them in, assuming the id server is down
		if ( svs.time - challenge->time > AUTHORIZE_TIMEOUT ) {
			Com_DPrintf( "authorize server timed out\n" );

			if (challenge->pingTime == -1) { challenge->pingTime = svs.time; }
			NET_OutOfBandPrint( NS_SERVER, challenge->adr, 
				"challengeResponse %i", challenge->challenge );
			return;
		}

		// otherwise send their ip to the authorize server
		if ( svs.authorizeAddress.type != NA_BAD ) {
			cvar_t	*fs;
			char	game[1024];

			Com_DPrintf( "sending getIpAuthorize for %s\n", NET_AdrToString( from ));
		
			strcpy(game, BASEGAME);
			fs = Cvar_Get ("fs_game", "", CVAR_INIT|CVAR_SYSTEMINFO );
			if (fs && fs->string[0] != 0) {
				strcpy(game, fs->string);
			}
		
			// the 0 is for backwards compatibility with obsolete sv_allowanonymous flags
			// getIpAuthorize <challenge> <IP> <game> 0 <auth-flag>
			NET_OutOfBandPrint( NS_SERVER, svs.authorizeAddress,
				"getIpAuthorize %i %i.%i.%i.%i %s 0 %s",  svs.challenges[i].challenge,
				from.ip[0], from.ip[1], from.ip[2], from.ip[3], game, sv_strictAuth->string );
		}
	}
	else
	{
		if (challenge->pingTime == -1) { challenge->pingTime = svs.time; }
		
		NET_OutOfBandPrint( NS_SERVER, challenge->adr, 
			"challengeResponse %i", challenge->challenge );
	}
#endif
}

#ifndef STANDALONE
/*
====================
SV_AuthorizeIpPacket

A packet has been returned from the authorize server.
If we have a challenge adr for that ip, send the
challengeResponse to it
====================
*/
void SV_AuthorizeIpPacket( netadr_t from ) {
	int		challenge;
	int		i;
	char	*s;
	char	*r;

	if ( !NET_CompareBaseAdr( from, svs.authorizeAddress ) ) {
		Com_Printf( "SV_AuthorizeIpPacket: not from authorize server\n" );
		return;
	}

	challenge = atoi( Cmd_Argv( 1 ) );

	for (i = 0 ; i < MAX_CHALLENGES ; i++) {
		if ( svs.challenges[i].challenge == challenge ) {
			break;
		}
	}
	if ( i == MAX_CHALLENGES ) {
		Com_Printf( "SV_AuthorizeIpPacket: challenge not found\n" );
		return;
	}

	// send a packet back to the original client
	if (svs.challenges[i].pingTime == -1) { svs.challenges[i].pingTime = svs.time; }
	s = Cmd_Argv( 2 );
	r = Cmd_Argv( 3 );			// reason

	if ( !Q_stricmp( s, "demo" ) ) {
		// they are a demo client trying to connect to a real server
		NET_OutOfBandPrint( NS_SERVER, svs.challenges[i].adr, "print\nServer is not a demo server\n" );
		// clear the challenge record so it won't timeout and let them through
		Com_Memset( &svs.challenges[i], 0, sizeof( svs.challenges[i] ) );
		return;
	}
	if ( !Q_stricmp( s, "accept" ) ) {
		NET_OutOfBandPrint( NS_SERVER, svs.challenges[i].adr, 
			"challengeResponse %i", svs.challenges[i].challenge );
		return;
	}
	if ( !Q_stricmp( s, "unknown" ) ) {
		if (!r) {
			NET_OutOfBandPrint( NS_SERVER, svs.challenges[i].adr, "print\nAwaiting CD key authorization\n" );
		} else {
			NET_OutOfBandPrint( NS_SERVER, svs.challenges[i].adr, "print\n%s\n", r);
		}
		// clear the challenge record so it won't timeout and let them through
		Com_Memset( &svs.challenges[i], 0, sizeof( svs.challenges[i] ) );
		return;
	}

	// authorization failed
	if (!r) {
		NET_OutOfBandPrint( NS_SERVER, svs.challenges[i].adr, "print\nSomeone is using this CD Key\n" );
	} else {
		NET_OutOfBandPrint( NS_SERVER, svs.challenges[i].adr, "print\n%s\n", r );
	}

	// clear the challenge record so it won't timeout and let them through
	Com_Memset( &svs.challenges[i], 0, sizeof( svs.challenges[i] ) );
}
#endif

/*
==================
SV_IsBanned

Check whether a certain address is banned
==================
*/

qboolean SV_IsBanned(netadr_t *from, qboolean isexception)
{
	int index, addrlen, curbyte, netmask, cmpmask;
	serverBan_t *curban;
	byte *addrfrom, *addrban;
	qboolean differed;
	
	if(from->type == NA_IP)
		addrlen = sizeof(from->ip);
	else if(from->type == NA_IP6)
		addrlen = sizeof(from->ip6);
	else
		return qfalse;

	if(!isexception)
	{
		// If this is a query for a ban, first check whether the client is excepted
		if(SV_IsBanned(from, qtrue))
			return qfalse;
	}
	
	for(index = 0; index < serverBansCount; index++)
	{
		curban = &serverBans[index];
		
		if(curban->isexception == isexception && from->type == curban->ip.type)
		{
			if(from->type == NA_IP)
			{
				addrfrom = from->ip;
				addrban = curban->ip.ip;
			}
			else
			{
				addrfrom = from->ip6;
				addrban = curban->ip.ip6;
			}
			
			differed = qfalse;
			curbyte = 0;
			
			for(netmask = curban->subnet; netmask > 7; netmask -= 8)
			{
				if(addrfrom[curbyte] != addrban[curbyte])
				{
					differed = qtrue;
					break;
				}
				
				curbyte++;
			}
			
			if(differed)
				continue;
				
			if(netmask)
			{
				cmpmask = (1 << netmask) - 1;
				cmpmask <<= 8 - netmask;
				
				if((addrfrom[curbyte] & cmpmask) == (addrban[curbyte] & cmpmask))
					return qtrue;
			}
			else
				return qtrue;
			
		}
	}
	
	return qfalse;
}

/*
==================
SV_ResolveIP2Loc

sv_ip2locHost->string is not touched by this function.  svs.ip2locAddress.type
will be set to NA_BAD when resolution fails or when sv_ip2locHost is empty.
==================
*/
void SV_ResolveIP2Loc( void ) {
	int	res;

	if (sv_ip2locHost->modified || // This will be true when server starts up even if sv_playerDBHost is empty.
			svs.ip2locAddress.type == 0) { // SV_Shutdown(), which gets called after 23 days, zeroes out svs.
		sv_ip2locHost->modified = qfalse;
		if (sv_ip2locHost->string[0]) {
			Com_Printf("Resolving ip2loc server address %s\n", sv_ip2locHost->string);
			res = NET_StringToAdr(sv_ip2locHost->string, &svs.ip2locAddress, NA_IP); // ip2loc service runs only on IPv4 addresses for now.
			if (!res) {
				// svs.ip2locAddress.type will be set to NA_BAD by NET_StringToAdr().
				Com_Printf("Couldn't resolve ip2loc server address: %s\n", sv_ip2locHost->string);
				return;
			}
			if (res == 2) {
				// Set the default port since it was not specified.
				svs.ip2locAddress.port = BigShort(10020);
			}
			Com_Printf("%s (ip2loc server) resolved to %s\n", sv_ip2locHost->string,
					NET_AdrToStringwPort(svs.ip2locAddress));
		}
		else {
			svs.ip2locAddress.type = NA_BAD;
		}
	}
}

/*
==================
SV_SendIP2LocPacketConditionally

Will only send a packet to the ip2loc service if sv_ip2locEnable is nonzero positive and
if svs.ip2locAddress.type is not NA_BAD after a resolution attempt.  If sv_ip2locPassword
is empty a packet will still be sent.  A packet will not be sent if this is a LAN address.
==================
*/
void SV_SendIP2LocPacketConditionally( client_t *cl ) {
	netadr_t	adr;

	if (sv_ip2locEnable->integer > 0) {
		SV_ResolveIP2Loc();
		adr = cl->netchan.remoteAddress;
		if (svs.ip2locAddress.type != NA_BAD && !Sys_IsLANAddress(adr)) {
			int ip2locChallenge = ((rand() << 16) ^ rand()) ^ svs.time;
			cl->ip2locChallenge = ip2locChallenge;
			Com_DPrintf("Sending ip2loc packet for client address %s\n", NET_AdrToString(adr));
			Com_DPrintf("    ...ip2loc server is %s\n", NET_AdrToStringwPort(svs.ip2locAddress));
			NET_OutOfBandPrint(NS_SERVER, svs.ip2locAddress, "ip2locRequest\n%s\ngetLocationForIP:%08x\n%s\n",
					sv_ip2locPassword->string, ip2locChallenge, NET_AdrToString(adr));
		}
	}
}

/*
=================
SV_SanitizeNameConditionally

Return codes:
 0 = success
 1 = userinfo overflow
-1 = illegal characters

This function only acts if sv_sanitizeNames is set to a nonzero positive value.  If that
is not the case, this function returns 0 immediately.

Converts a raw userinfo name into the "UrT server QVM name", which is a sanitized version of
the name.  The UrT server QVM name has spaces and colors stripped.
In case the raw name is missing or has zero length after sanitation, it becomes "UnnamedPlayer".
The sanitized name can be at most 20 characters long.  The logic here tries to accurately
mimic the logic in the UrT server QVM game engine.

Spaces are deleted before color stripping takes place; a space is not "gobbled up" by a
preceding carat ('^').  For example, the name "^ 7W" sanitizes to "W", not "7W", because
the '7' is gobbled up by the carat after the space is stripped.
The complete list of characters that are "pre-stripped" (like space) are
\0001 through \0032, \0034 through \0040, and \0176 through \0177.
Some characters should never appear in the userinfo name value for various reasons (for
example a certain character may never be transmitted by the client).  These characters are
\0033 (escape), \0042 (double quote), \0045 (percent),
\0073 (semicolon), \0134 (backslash), and all characters with the high bit set
(\0200 through \0377).  Even though these characters may in fact never be
encountered, if we do see them in the name, we don't sanitize the name and return -1.

Here are some notes Rambetter took while experimenting with sending handcrafted connect packets
with illegal characters in the userinfo string:

'\n' - Treated like space, but only possible in userinfo packet, not connect packet.

'\r' - It's possible to have this in a name, but only by handcrafting a
       connect or userinfo packet. The game engine strips the carriage return
       just like space.  However, it really messes up the "/rcon status"
       in ioquake3 (which doesn't strip the '\r' character at all)
       because everything after the '\r' in the name will be placed at the
       start of the line whether you're in native server console or in client
       console.

\033 - It's possible to insert the escape character into a name, but only
       by handcrafting a connect or userinfo packet.   The game code treats
       the escape kind of like a carat, where it deletes the character
       following escape.  However it gobbles the next character even if it is space.
       Furthermore, if the escape is at the end of the string, the game name is
       actually unpredictable (this must be some kind of really bad overflow bug
       or something).  Don't allow this in a name.

\042 - The double quote is impossible to insert into a name because it
       delimits an argument (the userinfo in this case).

\045 - The percent sign is coverted to '.' by the message read functions.  Cannot
       possibly appear in name.

\073 - It's actually possible to have a semicolon in the name in the ioquake3
       code with a handcrafted connect or userinfo packet.  The game code will
       rename the player to "badinfo".

\134 - The backslash separates fields in the userinfo so it cannot possibly
       be in the name.

\200 through \377 - Game code converts these to a '.' (period).  Disallow chars with
                    high bit set as a general policy.
=================
*/
int SV_SanitizeNameConditionally(char *userinfo) {
	static	char		charTypeMap[0400];
	static	qboolean	charMapInitialized = qfalse;
	char			*origRawName;
	char			*rawName;
	char			sanitizedName[20 + 1];
	int			sanitizedInx;
	int			i;
	char			code;
	qboolean		gobble;

	if (!(sv_sanitizeNames->integer > 0)) {
		return 0;
	}

	if (!charMapInitialized) {

		// Codes:
		// a - the null character
		// b - carat
		// c - space and other characters that are pre-stripped
		// d - normal characters that appear in name
		// e - disallowed characters

		charTypeMap[0000] = 'a';
		for (i = 0001; i <= 0032; i++) { charTypeMap[i] = 'c'; }
		charTypeMap[0033] = 'e'; // escape
		for (i = 0034; i <= 0040; i++) { charTypeMap[i] = 'c'; }
		charTypeMap[0041] = 'd';
		charTypeMap[0042] = 'e'; // double quote
		for (i = 0043; i <= 0044; i++) { charTypeMap[i] = 'd'; }
		charTypeMap[0045] = 'e'; // percent
		for (i = 0046; i <= 0072; i++) { charTypeMap[i] = 'd'; }
		charTypeMap[0073] = 'e'; // semicolon
		for (i = 0074; i <= 0133; i++) { charTypeMap[i] = 'd'; }
		charTypeMap[0134] = 'e'; // backslash
		charTypeMap[0135] = 'd';
		charTypeMap[0136] = 'b';
		for (i = 0137; i <= 0175; i++) { charTypeMap[i] = 'd'; }
		for (i = 0176; i <= 0177; i++) { charTypeMap[i] = 'c'; }
		for (i = 0200; i <= 0377; i++) { charTypeMap[i] = 'e'; } // high bit set

		charMapInitialized = qtrue;

	}

	origRawName = Info_ValueForKey(userinfo, "name");
	rawName = origRawName;
	sanitizedInx = 0;
	gobble = qfalse;

	while (qtrue) {
		if (sanitizedInx + 1 == sizeof(sanitizedName)) { break; }
		code = charTypeMap[rawName[0] & 0xff];
		switch (code) {
			case 'a': // end of string
				break;
			case 'b': // carat
				gobble = !gobble;
				rawName++;
				break;
			case 'c': // character to pre-strip
				rawName++;
				break;
			case 'd': // regular char in sanitized name
				if (gobble) { gobble = qfalse; }
				else { sanitizedName[sanitizedInx++] = rawName[0]; }
				rawName++;
				break;
			default: // 'e', disallowed character
				return -1;
		}
		if (code == 'a') break;
	}

	// Check rest of raw name for illegal chars.
	while (rawName[0]) {
		if (charTypeMap[rawName[0] & 0xff] == 'e') { return -1; }
		rawName++;
	}

	sanitizedName[sanitizedInx] = '\0';
	if (!sanitizedName[0]) {
		Q_strncpyz(sanitizedName, "UnnamedPlayer", sizeof(sanitizedName));
	}

	if (Info_SetValueForKey(userinfo, "name", sanitizedName)) {
		// Will actually only happen when name was empty, "UnnamedPlayer", because name grew in size.
		return 1;
	}
	return 0;
}

/*
==================
SV_CheckValidGear

Checks to see if a player's gear is valid.  If it's invalid, zero (which is a false value) is
returned.  If the gear is valid, the value returned is the number of extras the player has.
A secondary weapon is one extra, nades is one extra.  Each item counts as an extra as
well.  The number of extras for a valid loadout is between one and three, inclusive.
(In addition to that requirement, at least one item must be selected.)
The problem with gear is that invalid gear will cause the game engine to give the default
loadout without changing the gear in the userinfo.  Therefore, if we want to artificially limit
gear, we need to detect invalid gear strings.
==================
*/
int SV_CheckValidGear(const char *gear) {
	if (strlen(gear) != 7) {
		return 0;
	}

	// The first character is the sidearm.  It must be one of the following:
	//   'F' = Beretta
	//   'G' = DE
	// The second character is the primary weapon.  It must be one of the following:
	//   'M' = G36
	//   'a' = AK103
	//   'L' = LR300
	//   'K' = nade launcher
	//   'N' = PSG-1
	//   'Z' = SR8
	//   'c' = Negev
	//   'e' = M4
	//   'H' = Spas
	//   'I' = Mp5k
	//   'J' = Ump45
	// The third character is the seconday weapon.  It must be one of the following:
	//   'A' = empty
	//   'H' = Spas
	//   'I' = Mp5k
	//   'J' = Ump45
	// The fourth character is the grenade.  It must be one of the following:
	//   'A' = empty
	//   'O' = HE Grenade
	//   'Q' = Smoke Grenade
	// The last 3 characters are the items.  No item may be repeated more than once except 'A'.
	// All of the 'A' items must be at the very end of the string.
	//   'A' = empty
	//   'R' = kevlar
	//   'S' = tac goggles
	//   'T' = medkit
	//   'U' = silencer
	//   'V' = laser
	//   'W' = helmet
	//   'X' = extra ammo
	// Other rules:
	// - No secondary weapon with Negev.
	// - Counting secondary as 1, nade as 1, and item as 1, max total count allowed is 3.
	// - Minimum one item.

	int		extrasCount = 0;	// counts the secondary, nade, and items
	int		itemCount = 0;		// just itmes
	int		items = 0x00;		// bits representing items
	char		*pistols = "FG";
	char		*primaries = "MaLKNZceHIJ";
	char		*secondaries = "AHIJ";
	char		*nades = "AOQ";
	qboolean	negev = qfalse;
	qboolean	emptyItemSeen = qfalse;
	int		i, itemBit;

	i = strlen(pistols);
	while (qtrue) {
		if (--i < 0) { return 0; }
		if (gear[0] == pistols[i]) { break; }
	}
	i = strlen(primaries);
	while (qtrue) {
		if (--i < 0) { return 0; }
		if (gear[1] == primaries[i]) {
			if (gear[1] == 'c') { negev = qtrue; }
			break;
		}
	}
	i = strlen(secondaries);
	while (qtrue) {
		if (--i < 0) { return 0; }
		if (gear[2] == secondaries[i]) {
			if (gear[2] != 'A') { extrasCount++; }
			break;
		}
	}
	if (negev && extrasCount) { return 0; }
	i = strlen(nades);
	while (qtrue) {
		if (--i < 0) { return 0; }
		if (gear[3] == nades[i]) {
			if (gear[3] != 'A') { extrasCount++; }
			break;
		}
	}
	for (i = 4; i < 7; i++) {
		if (gear[i] == 'A') {
			emptyItemSeen = qtrue;
			continue;
		}
		if (!('R' <= gear[i] && gear[i] <= 'X')) {
			return 0; // Unrecognized item.
		}
		if (emptyItemSeen) {
			return 0; // Real item after empty item.
		}
		itemBit = 0x01 << (gear[i] - 'R');
		if (items & itemBit) {
			return 0; // Item appeared a second time.
		}
		itemCount++;
		extrasCount++;
		items |= itemBit;
	}
	if (!itemCount) {
		return 0; // Must have at least one item.
		// This condition is actually handled when we return extrasCount,
		// but I add it here for clarity.
	}
	if (extrasCount > 3) {
		return 0;
	}
	return extrasCount;
}

/*
==================
SV_ConditionalNoKevlar

If sv_noKevlar is set to a nonzero positive value, removes kevlar from the player's
userinfo string.  If the gear is invalid to begin with, gives the player the default
loadout but without kevlar.  Tries to give the player medkit if sv_noKevlar is set.

The return value may be somewhat cryptic.  It returns false when we need to kick this
player because their userinfo string exceeded the maximum length.
==================
*/
qboolean SV_ConditionalNoKevlar(char *userinfo) {
	char	*gearStr;
	int    	extrasCount, items, i, j;
	char	gear[8];

	if (sv_noKevlar-> integer > 0) {
		gearStr = Info_ValueForKey(userinfo, "gear");
		extrasCount = SV_CheckValidGear(gearStr);
		if (!extrasCount) {
			Com_DPrintf("Player with invalid gear \"%s\"\n", gearStr);
			// FLHARWA is the default gear the client gives you when you start out.
			// The default the server gives you in case yours is invalid is the same as above
			// only minus the Spas.  So that leaves FLAARWA.  We take away the kevlar
			// and add medkit to get FLAATWA.
			if (Info_SetValueForKey(userinfo, "gear", "FLAATWA")) {
				return qfalse;
			}
			return qtrue;
		}
		// Items:
		// 0x00 'A' = empty
		// 0x01 'R' = kevlar
		// 0x02 'S' = tac goggles
		// 0x04 'T' = medkit
		// 0x08 'U' = silencer
		// 0x10 'V' = laser
		// 0x20 'W' = helmet
		// 0x40 'X' = extra ammo
		items = 0x00;
		for (i = 0; i < 4; i++) { gear[i] = gearStr[i]; } // Copy guns and nades.
		for (i = 4; i < 7; i++) {
			if ('R' <= gearStr[i] && gearStr[i] <= 'X') { // Or check that it isn't 'A'.
				items |= 0x01 << (gearStr[i] - 'R');
			}
		}
		if (items & 0x01) { // Had kevlar.
			items &= (~0x01); // Remove kevlar.
			extrasCount--;
		}
		if (extrasCount < 3) {
			if (!(items & 0x04)) { // No medkit.
				// Since this "no kevlar" patch is used on jump servers mostly,
				// we'll equip the player with a medkit.
				// IMPORTANT: If you remove this code, make sure you do a check
				// that there is at least one item ('R' through 'X') present.
				items |= 0x04;
				extrasCount++;
			}
		}
		i = 4;
		for (j = 0; j < 7; j++) {
			if ((items & (0x01 << j)) != 0) {
				gear[i++] = 'R' + j;
			}
		}
		while (i < 7) {
			gear[i++] = 'A';
		}
		gear[i] = '\0';
		Info_SetValueForKey(userinfo, "gear", gear); // Won't overflow, old gear was same length.
	}
	return qtrue;
}

/*
==================
SV_ApproveGuid

Returns a false value if and only if a client with this cl_guid
should not be allowed to enter the server.  The check is only made
if sv_requireValidGuid is nonzero positive, otherwise every guid passes.

A cl_guid string must have length 32 and consist of characters
'0' through '9' and 'A' through 'F'.
==================
*/
qboolean SV_ApproveGuid( const char *guid) {
	int	i;
	char	c;
	int	length;

	if (sv_requireValidGuid->integer > 0) {
		length = strlen(guid); // We could avoid this extra linear-time computation with more complex code.
		if (length != 32) { return qfalse; }
		for (i = 31; i >= 0;) {
			c = guid[i--];
			if (!(('0' <= c && c <= '9') ||
				('A' <= c && c <= 'F'))) {
				return qfalse;
			}
		}
	}
	return qtrue;
}

/*
==================
SV_ResolvePlayerDB

sv_playerDBHost->string is not touched by this function.  svs.playerDatabaseAddress.type
will be set to NA_BAD when resolution fails or when sv_playerDBHost is empty.
==================
*/
void SV_ResolvePlayerDB( void ) {
	int	res;

	if (sv_playerDBHost->modified || // This will be true when server starts up even if sv_playerDBHost is empty.
			svs.playerDatabaseAddress.type == 0) { // SV_Shutdown(), which gets called after 23 days, zeroes out svs.
		sv_playerDBHost->modified = qfalse;
		if (sv_playerDBHost->string[0]) {
			Com_Printf("Resolving playerdb server address %s\n", sv_playerDBHost->string);
			res = NET_StringToAdr(sv_playerDBHost->string, &svs.playerDatabaseAddress, NA_IP); // playerdb only listens on IPv4 addresses for now.
			if (!res) {
				// svs.playerDatabaseAddress.type will be set to NA_BAD by NET_StringToAdr().
				Com_Printf("Couldn't resolve playerdb address: %s\n", sv_playerDBHost->string);
				return;
			}
			if (res == 2) {
				// Set the default port since it was not specified.
				svs.playerDatabaseAddress.port = BigShort(10030);
			}
			Com_Printf("%s (playerdb) resolved to %s\n", sv_playerDBHost->string, NET_AdrToStringwPort(svs.playerDatabaseAddress));
		}
		else {
			svs.playerDatabaseAddress.type = NA_BAD;
		}
	}
}

/*
==================
SV_SendUserinfoToPlayerDBConditionally

Will only send a packet to the player database if sv_playerDBUserInfo is nonzero positive and
if svs.playerDatabaseAddress.type is not NA_BAD after a resolution attempt.  If the player
database password is empty a packet will still be sent.
==================
*/
void SV_SendUserinfoToPlayerDBConditionally(const char *userinfo) {
	if (sv_playerDBUserInfo->integer > 0) {
		SV_ResolvePlayerDB();
		if (svs.playerDatabaseAddress.type != NA_BAD) {
			Com_DPrintf("Sending clientUserInfo packet to playerdb\n");
			NET_OutOfBandPrint(NS_SERVER,
				svs.playerDatabaseAddress,
				"playerDBRequest\n%s\nclientUserInfo\n%s\n",
				svs.playerDatabasePassword,
				userinfo);
		}
	}
}

/*
==================
SV_DirectConnect

A "connect" OOB command has been received
==================
*/

void SV_DirectConnect( netadr_t from ) {
	char		userinfo[MAX_INFO_STRING];
	int			challengeIndex;
	int			i;
	client_t	*loopcl, *newcl;
	sharedEntity_t *ent;
	int			clientNum;
	int			version;
	int			qport;
	int			challenge;
	char		*password;
	int			startIndex;
	intptr_t		denied;
	int			count;

	Com_DPrintf ("SVC_DirectConnect ()\n");

	// Cmd_Argv(1) would actually never overflow userinfo; the MSG_ReadStringLine()
	// call at the top of SV_ConnectionlessPacket() guarantees this, as long as
	// MAX_STRING_CHARS is less than or equal to MAX_INFO_STRING.
	Q_strncpyz(userinfo, Cmd_Argv(1), sizeof(userinfo));
	if (!Info_Validate(userinfo)) {
		NET_OutOfBandPrint(NS_SERVER, from,
				"print\nIllegal characters in userinfo string.\n");
		Com_DPrintf("Connect packet from %s has userinfo with illegal characters\n",
				NET_AdrToStringwPort(from));
		return;
	}

	version = atoi(Info_ValueForKey(userinfo, "protocol"));
	if (version != PROTOCOL_VERSION) {
		NET_OutOfBandPrint(NS_SERVER, from, "print\nServer uses protocol version %i.\n", PROTOCOL_VERSION);
		Com_DPrintf("Rejected connect from %s, wrong protocol version %i\n",
				NET_AdrToStringwPort(from), version);
		return;
	}

	// At this point, we must assume that the "from" address may be spoofed.  We have to assume
	// that a malicious client may start spamming the server with incredible amounts of connect
	// packets.  Therefore, before performing any CPU-intensive operations, we're
	// going to check to see that the challenge is correct.  Then we will, for one, know that the
	// "from" address is not spoofed.  Later on, we can add throttling logic to prevent too many
	// connect packets in a certain timeframe from a single IP address.

	// So, first check the challenge to verify the "from" address.
	challenge = atoi(Info_ValueForKey(userinfo, "challenge"));
	challengeIndex = 0;
	if (!NET_IsLocalAddress(from)) {
		for (; challengeIndex < MAX_CHALLENGES; challengeIndex++) {
			if (NET_CompareAdr(from, svs.challenges[challengeIndex].adr)) {
				if (challenge == svs.challenges[challengeIndex].challenge)
					break;
			}
		}
		if (challengeIndex == MAX_CHALLENGES) {
			NET_OutOfBandPrint(NS_SERVER, from, "print\nNo or bad challenge for address.\n");
			return;
		}
		if (sv_limitConnectPacketsPerIP->integer > 0) {
			// We limit 4 connect packets from one IP address in the past 6 seconds.
			receipt_t *connect = &svs.connects[0];
			int oldest = 0;
			int oldestTime = 0x7fffffff;
			int connectPacks = 1; // Count this packet as one.
			for (i = 0; i < MAX_CONNECTS; i++, connect++) {
				if (NET_CompareBaseAdr(from, connect->adr) &&
						connect->time + 6000 > svs.time) {
					connectPacks++;
				}
				if (connect->time < oldestTime) {
					oldestTime = connect->time;
					oldest = i;
				}
			}
			if (connectPacks > 4) {
				Com_DPrintf("Ignoring connect packet from %s, seems to be spamming\n",
						NET_AdrToStringwPort(from));
				return;
			}
			connect = &svs.connects[oldest];
			connect->adr = from;
			connect->time = svs.time;
		}
	}

	// At this point we have verified that the "from" address has not been spoofed.
	
	// Check whether this client is banned.
	if(SV_IsBanned(&from, qfalse))
	{
		NET_OutOfBandPrint(NS_SERVER, from, "print\nYou are banned from this server.\n");
		return;
	}

	qport = atoi( Info_ValueForKey( userinfo, "qport" ) );

	// quick reject
	for (i = 0, loopcl = svs.clients; i < sv_maxclients->integer; i++, loopcl++) {
		if (loopcl->state == CS_FREE) {
			continue;
		}
		if (NET_CompareBaseAdr(from, loopcl->netchan.remoteAddress)
				&& (loopcl->netchan.qport == qport
					|| from.port == loopcl->netchan.remoteAddress.port)) {
			if ((svs.time - loopcl->lastConnectTime) < (sv_reconnectlimit->integer * 1000)) {
				Com_DPrintf ("%s:reconnect rejected : too soon\n", NET_AdrToString (from));
				return;
			}
			break;
		}
	}

	if ( !NET_IsLocalAddress (from) ) {
		int		ping;

		// This is where the challenge check code used to be.  We moved it to the top of SV_DirectConnect().

		// Note that it is totally possible to flood the console and qconsole.log by being rejected
		// (high ping, ban, server full, or other) and repeatedly sending a connect packet against the same
		// challenge.  Prevent this situation by only logging the first time we hit SV_DirectConnect()
		// for this challenge.
		if (!svs.challenges[challengeIndex].connected) {
			ping = svs.time - svs.challenges[challengeIndex].pingTime;
			svs.challenges[challengeIndex].challengePing = ping;
			Com_Printf("Client %i connecting with %i challenge ping\n", challengeIndex, ping);
		}
		else {
			ping = svs.challenges[challengeIndex].challengePing;
			Com_DPrintf("Client %i connecting again with %i challenge ping\n", challengeIndex, ping);
		}
		qboolean firstConnect = !svs.challenges[challengeIndex].connected;
		svs.challenges[challengeIndex].connected = qtrue;

		// never reject a LAN client based on ping
		if ( !Sys_IsLANAddress( from ) ) {

			// The player database won't be able to make sense of a userinfo with an invalid
			// guid (will just drop it), so perform this check before sending the userinfo.
			// SV_ApproveGuid already checks for sv_requireValidGuid.
			if (!SV_ApproveGuid(Info_ValueForKey(userinfo, "cl_guid"))) {
				NET_OutOfBandPrint(NS_SERVER, from, "print\nGet legit, bro.\n");
				Com_DPrintf("Invalid cl_guid, rejected connect from %s\n", NET_AdrToStringwPort(from));
				return;
			}

			// Send a packet to the player database with the userinfo string at
			// the very earliest opportunity, before we potentially kick the player
			// for various reasons.  We want to have a record of the player connecting
			// even if they're not able to connect because of being being banned or high ping.
			// There is a possibility of a flood attack by sending a bunch of connect packets
			// for the same challenge.  That's why we send only one userinfo per challenge.
			// Note: It's good to send the userinfo to the playerdb only on first connect to avoid the
			// possibility of connect packet spam with a different guid and name every time;
			// this would fill up the database very quickly (much quicker than getting a challenge
			// for every connect, because that requires two round trips).  However, there is
			// an improbable possibility that the client sends a second connect packet with a
			// different guid and name.  Then, unless the client later sends a userinfo packet,
			// the guid and name will never be stored in the database.  This is really a bug.
			// But maybe we don't want that player in the database after all because he's clearly
			// a hacking guid-changer, so maybe not that much of a big deal.
			if (firstConnect && sv_playerDBUserInfo->integer > 0) {
				// We check sv_playerDBUserInfo above because we don't munge their userinfo
				// unless it's positive.  Override their IP address early so that the player
				// database gets the correct one.
				char *ip = (char *) NET_AdrToString(from);
				if (Info_SetValueForKeySilent(userinfo, "ip", ip)) {
					NET_OutOfBandPrint(NS_SERVER, from, "print\nUserinfo overflow.\n");
					Com_DPrintf("Userinfo overflow on trying to insert IP, from %s\n",
							NET_AdrToStringwPort(from));
					return;
				}
				SV_SendUserinfoToPlayerDBConditionally(userinfo);
			}

			if (svs.challenges[challengeIndex].permabanned) {
				if (sv_permaBanBypass->string[0] &&
						!strcmp(Info_ValueForKey(userinfo, "permabanbypass"), sv_permaBanBypass->string)) {
					svs.challenges[challengeIndex].permabanned = qfalse; // Not sure if this is even necessary.
				}
				else {
					NET_OutOfBandPrint(NS_SERVER, svs.challenges[challengeIndex].adr, "print\nPermabanned.\n" );
					Com_DPrintf("Permabanned\n");
					return;
				}
			}
			///////////////////////////////////////////////////////
			// separator for playerdb.patch and reconnectwait.patch
			///////////////////////////////////////////////////////
			int	reconnectWaitTime;
			if ((reconnectWaitTime = sv_reconnectWaitTime->integer) > 0) {
				if (reconnectWaitTime > 300) { // 5 minutes.
					reconnectWaitTime = 300;
				}
				reconnectWaitTime *= 1000;
				drop_t		*drop;
				drop = &svs.drops[0];
				for (i = 0; i < MAX_DROPS; i++, drop++) {
					if (NET_CompareAdr(from, drop->adr)) {
						int	reconnectAbsoluteTime;
						reconnectAbsoluteTime = drop->time + drop->waitFactor * reconnectWaitTime;
						if (reconnectAbsoluteTime > svs.time) {
							NET_OutOfBandPrint(NS_SERVER, from,
								"print\nReconnecting, please wait...\n");
							Com_DPrintf("Client from %s waiting to reconnect\n",
								NET_AdrToStringwPort(from));
							return;
						}
					}
				}
			}

			if ( sv_minPing->value && ping < sv_minPing->value ) {
				NET_OutOfBandPrint( NS_SERVER, from, "print\nServer is for high pings only\n" );
				Com_DPrintf ("Client %i rejected on a too low ping\n", challengeIndex);
				return;
			}
			if ( sv_maxPing->value && ping > sv_maxPing->value ) {
				NET_OutOfBandPrint( NS_SERVER, from, "print\nServer is for low pings only\n" );
				Com_DPrintf ("Client %i rejected on a too high ping\n", i);
				return;
			}

			// I guess we're not checking for the qport 1337 hack on LAN clients.
			if (sv_block1337->integer > 0 && qport == 1337) {
				NET_OutOfBandPrint(NS_SERVER, from, "print\nThis server is not for wussies.\n");
				Com_DPrintf("1337 qport, rejected connect from %s\n", NET_AdrToString(from));
				return;
			}
		}
	}

	// if there is already a slot for this ip, reuse it
	for (i = 0, loopcl = svs.clients; i < sv_maxclients->integer; i++, loopcl++) {
		if (loopcl->state == CS_FREE) {
			continue;
		}
		if (NET_CompareBaseAdr(from, loopcl->netchan.remoteAddress)
				&& (loopcl->netchan.qport == qport
					|| from.port == loopcl->netchan.remoteAddress.port)) {
			Com_Printf ("%s:reconnect\n", NET_AdrToString (from));
			newcl = loopcl;

			// this doesn't work because it nukes the players userinfo

//			// disconnect the client from the game first so any flags the
//			// player might have are dropped
//			VM_Call( gvm, GAME_CLIENT_DISCONNECT, newcl - svs.clients );
			//
			goto gotnewcl;
		}
	}

	// find a client slot
	// if "sv_privateClients" is set > 0, then that number
	// of client slots will be reserved for connections that
	// have "password" set to the value of "sv_privatePassword"
	// Info requests will report the maxclients as if the private
	// slots didn't exist, to prevent people from trying to connect
	// to a full server.
	// This is to allow us to reserve a couple slots here on our
	// servers so we can play without having to kick people.

	// check for privateClient password
	password = Info_ValueForKey( userinfo, "password" );
	if ( !strcmp( password, sv_privatePassword->string ) ) {
		startIndex = 0;
	} else {
		// skip past the reserved slots
		startIndex = sv_privateClients->integer;
	}

	qboolean checkMaxClientsPerIP = ((sv_maxClientsPerIP->integer > 0) && (!Sys_IsLANAddress(from)));
	int clientsThisIP = 0;
	newcl = NULL;
	for ( i = startIndex; i < sv_maxclients->integer ; i++ ) {
		loopcl = &svs.clients[i];
		if (loopcl->state == CS_FREE) {
			if (newcl == NULL) { newcl = loopcl; }
		}
		// In this "else" statement, the client is _not_ CS_FREE.  Even if it's CS_ZOMBIE, count the
		// the client if the IP address matches.
		else if (checkMaxClientsPerIP && NET_CompareBaseAdr(from, loopcl->netchan.remoteAddress)) {
			clientsThisIP++;
		}
	}
	if (newcl != NULL && // If server is full, give them the "server full" message below.
			checkMaxClientsPerIP && clientsThisIP >= sv_maxClientsPerIP->integer) {
		NET_OutOfBandPrint(NS_SERVER, from, "print\nCurrently too many clients from your IP address.\n");
		Com_DPrintf("Recected a connect, too many clients from IP address %s\n", NET_AdrToString(from));
		return;
	}

	if ( !newcl ) {
		if ( NET_IsLocalAddress( from ) ) {
			count = 0;
			for ( i = startIndex; i < sv_maxclients->integer ; i++ ) {
				loopcl = &svs.clients[i];
				if (loopcl->netchan.remoteAddress.type == NA_BOT) {
					count++;
				}
			}
			// if they're all bots
			if (count >= sv_maxclients->integer - startIndex) {
				SV_DropClient(&svs.clients[sv_maxclients->integer - 1], "only bots on server");
				newcl = &svs.clients[sv_maxclients->integer - 1];
			}
			else {
				Com_Error( ERR_FATAL, "server is full on local connect\n" );
				return;
			}
		}
		else {
			NET_OutOfBandPrint( NS_SERVER, from, "print\nServer is full.\n" );
			Com_DPrintf ("Rejected a connection.\n");
			return;
		}
	}

gotnewcl:	
	// build a new connection
	// accept the new client
	// this is the only place a client_t is ever initialized
	Com_Memset(newcl, 0, sizeof(client_t)); // Resets reliableSequence, reliableAcknowledge (even for reconnect).
	clientNum = newcl - svs.clients;
	ent = SV_GentityNum( clientNum );
	newcl->gentity = ent;

	// save the challenge
	newcl->challenge = challenge;

	// save the address
	Netchan_Setup (NS_SERVER, &newcl->netchan , from, qport);
	// init the netchan queue
	newcl->netchan_end_queue = &newcl->netchan_start_queue;

	// save the userinfo
	Q_strncpyz( newcl->userinfo, userinfo, sizeof(newcl->userinfo) );

	// We need to override the ip in the userinfo before we make the VM call to GAME_CLIENT_CONNECT.
	char *userinfoChangedRtn = SV_UserinfoChanged(newcl); // Overrides ip, does other cleaning.
	if (userinfoChangedRtn && userinfoChangedRtn[0]) {
		NET_OutOfBandPrint(NS_SERVER, from, "print\n%s\n", userinfoChangedRtn);
		Com_DPrintf("Rejected connection due to SV_UserinfoChanged failure: %s\n", userinfoChangedRtn);
		return;
	}

	// get the game a chance to reject this connection or modify the userinfo
	// Note, this VM call never modifies the client's userinfo on the ioquake3 side of things
	// (true at least for Urban Terror).  That's why it was safe to move the SV_UserinfoChanged()
	// call to right above here (it used to be just below).
	denied = VM_Call( gvm, GAME_CLIENT_CONNECT, clientNum, qtrue, qfalse );
	if ( denied ) {
		SV_DropClient(newcl, "denied by game"); // Make this consistent with the SV_UserinfoChanged() call above.
		// we can't just use VM_ArgPtr, because that is only valid inside a VM_Call
		char *str = VM_ExplicitArgPtr( gvm, denied );

		NET_OutOfBandPrint( NS_SERVER, from, "print\n%s\n", str );
		Com_DPrintf ("Game rejected a connection: %s\n", str);
		return;
	}

	// send the connect packet to the client
	NET_OutOfBandPrint( NS_SERVER, from, "connectResponse" );

	Com_DPrintf( "Going from CS_FREE to CS_CONNECTED for %s\n", newcl->name );

	newcl->state = CS_CONNECTED;
	newcl->nextSnapshotTime = svs.time;
	newcl->lastPacketTime = svs.time;
	newcl->lastConnectTime = svs.time;
	
	// when we receive the first packet from the client, we will
	// notice that it is from a different serverid and that the
	// gamestate message was not just sent, forcing a retransmit
	newcl->gamestateMessageNum = -1;

	SV_SendIP2LocPacketConditionally(newcl);

	// if this was the first client on the server, or the last client
	// the server can hold, send a heartbeat to the master.
	count = 0;
	for (i = 0; i < sv_maxclients->integer; i++) {
		if ( svs.clients[i].state >= CS_CONNECTED ) {
			count++;
		}
	}
	if ( count == 1 || count == sv_maxclients->integer ) {
		SV_Heartbeat_f();
	}
}


/*
=====================
SV_DropClient

Called when the player is totally leaving the server, either willingly
or unwillingly.  This is NOT called if the entire server is quiting
or crashing -- SV_FinalMessage() will handle that
=====================
*/
void SV_DropClient( client_t *drop, const char *reason ) {
	int		i;
	//challenge_t	*challenge; // See note below.

	if ( drop->state == CS_ZOMBIE ) {
		return;		// already dropped
	}

	// Setting the connected state of the challenge to false is totally unnecessary and only
	// leads to all sorts of anomalies.  Here is my explanation of why commenting this out will
	// not break anything.  When a player connects and reaches the point in SV_DirectConnect()
	// where the challenge connected state is set to true, the client may be dropped due to
	// high ping, being banned by the VM, a server full, or other reasons.  In these cases,
	// the client struct is never created for the connection and the player is never dropped
	// using SV_DropClient().  SV_DropClient() happens to be the only place where a challenge's
	// connected state is set to false, other than in SV_GetChallenge() where a new challenge
	// is allocated.  So, when the player is dropped for high ping (and for other other reasons
	// stated above) and tries to reconnect from scratch using the getchallenge packet, a
	// new challenge structure is created (the old one doesn't get reused).  There is no
	// more harm done in not reusing a challenge if a player happens to connect all the way
	// because connecting all the way happens much less frequently.  The challenge does not
	// contain any important information that we need when the client tries to reconnect.
	// Now what commenting this section out improves: the challenge fields (challenge, time,
	// and pingTime) will all be computed from scratch, just like how it should be.
	// If the challenge struct is extended in various patches, the properties of the challenge
	// won't get inherited by subsequent connections by the same client.  This is much cleaner.
	/*
	if (drop->netchan.remoteAddress.type != NA_BOT) {
		// see if we already have a challenge for this ip
		challenge = &svs.challenges[0];

		for (i = 0 ; i < MAX_CHALLENGES ; i++, challenge++) {
			if ( NET_CompareAdr( drop->netchan.remoteAddress, challenge->adr ) ) {
				challenge->connected = qfalse;
				break;
			}
		}
	}
	*/

	// This check for CS_FREE was added because in SV_DirectConnect(), the SV_UserinfoChanged()
	// call was moved before VM_Call(GAME_CLIENT_CONNECT).  It's possible that that invocation
	// of SV_UserinfoChanged() will overflow the userinfo string (or some other error will
	// happen), resulting in a SV_DropClient() before the client is even connected according to
	// the VM.  Since the client isn't connected as far as the VM is concerned, we don't need
	// to continue to the rest of this SV_DropClient() call.  However, if you look below, the
	// client userinfo string is cleared, and that seems to be the only important thing that
	// we would skip over, so add that here.
	// Note: SV_DropClient() is now also called in SV_DirectConnect() if the client is denied
	// from connecting to the VM.  In that case the state will also be CS_FREE.
	if (drop->state == CS_FREE) {
		SV_SetUserinfo(drop - svs.clients, "");
		return;
	}

	if (drop->netchan.remoteAddress.type != NA_BOT) {
		drop_t		*dropRecord;
		int		oldest;
		int		oldestTime;
		dropRecord = &svs.drops[0];
		oldest = 0;
		oldestTime = 0x7fffffff;
		for (i = 0; i < MAX_DROPS; i++, dropRecord++) {
			if (NET_CompareAdr(drop->netchan.remoteAddress, dropRecord->adr)) {
				// Try to reuse a drop in case they're flooding and the attacker can't figure out
				// to change the port each time.  (We could really get rid of this actually.)
				break;
			}
			if (dropRecord->time < oldestTime) {
				oldestTime = dropRecord->time;
				oldest = i;
			}
		}
		if (i == MAX_DROPS) {
			dropRecord = &svs.drops[oldest];
		}
		dropRecord->adr = drop->netchan.remoteAddress;
		dropRecord->time = svs.time;
		dropRecord->waitFactor = 1;
		if (!strcmp("was kicked", reason)) { // Vote kicked or kicked/banned by admin.
			dropRecord->waitFactor = 2;
		}
		if (!strcmp("Teamkilling is bad m'kay?", reason)) { // Booted for team killing.
			dropRecord->waitFactor = 3;
		}
	}

	// Kill any download
	SV_CloseDownload( drop );

	// tell everyone why they got dropped
	SV_SendServerCommand( NULL, "print \"%s" S_COLOR_WHITE " %s\n\"", drop->name, reason );


	if (drop->download)	{
		FS_FCloseFile( drop->download );
		drop->download = 0;
	}

	// call the prog function for removing a client
	// this will remove the body, among other things
	VM_Call( gvm, GAME_CLIENT_DISCONNECT, drop - svs.clients );

	// add the disconnect command
	SV_SendServerCommand( drop, "disconnect \"%s\"", reason);

	if ( drop->netchan.remoteAddress.type == NA_BOT ) {
		SV_BotFreeClient( drop - svs.clients );
	}

	// nuke user info
	SV_SetUserinfo( drop - svs.clients, "" );
	
	Com_DPrintf( "Going to CS_ZOMBIE for %s\n", drop->name );
	drop->state = CS_ZOMBIE;		// become free in a few seconds

	// if this was the last client on the server, send a heartbeat
	// to the master so it is known the server is empty
	// send a heartbeat now so the master will get up to date info
	// if there is already a slot for this ip, reuse it
	for (i=0 ; i < sv_maxclients->integer ; i++ ) {
		if ( svs.clients[i].state >= CS_CONNECTED ) {
			break;
		}
	}
	if ( i == sv_maxclients->integer ) {
		SV_Heartbeat_f();
	}
}

/*
================
SV_SendClientGameState

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each new map load.

It will be resent if the client acknowledges a later message but has
the wrong gamestate.
================
*/
void SV_SendClientGameState( client_t *client ) {
	int			start;
	entityState_t	*base, nullstate;
	msg_t		msg;
	byte		msgBuffer[MAX_MSGLEN];

 	Com_DPrintf ("SV_SendClientGameState() for %s\n", client->name);
	Com_DPrintf( "Going from CS_CONNECTED to CS_PRIMED for %s\n", client->name );
	client->state = CS_PRIMED;
	client->pureAuthentic = 0;
	client->gotCP = qfalse;

	// when we receive the first packet from the client, we will
	// notice that it is from a different serverid and that the
	// gamestate message was not just sent, forcing a retransmit
	client->gamestateMessageNum = client->netchan.outgoingSequence;

	MSG_Init( &msg, msgBuffer, sizeof( msgBuffer ) );

	// NOTE, MRE: all server->client messages now acknowledge
	// let the client know which reliable clientCommands we have received
	MSG_WriteLong( &msg, client->lastClientCommand );

	// send any server commands waiting to be sent first.
	// we have to do this cause we send the client->reliableSequence
	// with a gamestate and it sets the clc.serverCommandSequence at
	// the client side
	SV_UpdateServerCommandsToClient( client, &msg );

	// send the gamestate
	MSG_WriteByte( &msg, svc_gamestate );
	MSG_WriteLong( &msg, client->reliableSequence );

	// write the configstrings
	for ( start = 0 ; start < MAX_CONFIGSTRINGS ; start++ ) {
		if (sv.configstrings[start][0]) {
			MSG_WriteByte( &msg, svc_configstring );
			MSG_WriteShort( &msg, start );
			MSG_WriteBigString( &msg, sv.configstrings[start] );
		}
	}

	// write the baselines
	Com_Memset( &nullstate, 0, sizeof( nullstate ) );
	for ( start = 0 ; start < MAX_GENTITIES; start++ ) {
		base = &sv.svEntities[start].baseline;
		if ( !base->number ) {
			continue;
		}
		MSG_WriteByte( &msg, svc_baseline );
		MSG_WriteDeltaEntity( &msg, &nullstate, base, qtrue );
	}

	MSG_WriteByte( &msg, svc_EOF );

	MSG_WriteLong( &msg, client - svs.clients);

	// write the checksum feed
	MSG_WriteLong( &msg, sv.checksumFeed);

	// deliver this to the client
	SV_SendMessageToClient( &msg, client );
}

/*
===================
SV_UrT_FreeForAll_Kludge

In UrT when g_gametype is switched to "Free for All" from
a team-based gametype, bad things happen: Players will be
on colored teams but colored teams don't exist so they do
not show up on the score board. They also seem to change
colors when shot. Fun and all, but not very convenient if
you want to run a mixed-mode server. Thus this kludge.
===================
*/
static void SV_UrT_FreeForAll_Kludge(client_t *client)
{
        int slot, team;
        playerState_t *state;
    
        if (Cvar_VariableValue("g_gametype") == GT_FFA) {
                slot = client - svs.clients;
                state = SV_GameClientNum(slot);
                team = state->persistant[PERS_TEAM];
                Com_DPrintf("SV_UrT_FreeForAll_Kludge() found team %i for player %i\n", team, slot);
        
                if (team == TEAM_RED || team == TEAM_BLUE) {
                        Cmd_ExecuteString (va("forceteam %i spectator", slot));
                        Cmd_ExecuteString (va("forceteam %i ffa", slot));
                        Com_Printf("SV_UrT_FreeForAll_Kludge() forced player %i to team ffa\n", slot);
                }
        }
}

/*
==================
SV_ClientEnterWorld
==================
*/
void SV_ClientEnterWorld( client_t *client, usercmd_t *cmd ) {
	int		clientNum;
	sharedEntity_t *ent;

	Com_DPrintf( "Going from CS_PRIMED to CS_ACTIVE for %s\n", client->name );
	client->state = CS_ACTIVE;

	// resend all configstrings using the cs commands since these are
	// no longer sent when the client is CS_PRIMED
	SV_UpdateConfigstrings( client );

	// set up the entity for the client
	clientNum = client - svs.clients;
	ent = SV_GentityNum( clientNum );
	ent->s.number = clientNum;
	client->gentity = ent;

	client->deltaMessage = -1;
	client->nextSnapshotTime = svs.time;	// generate a snapshot immediately
	client->lastUsercmd = *cmd;

	// call the game begin function
	VM_Call( gvm, GAME_CLIENT_BEGIN, client - svs.clients );
    // this has to be called *after* the UrT game code; it's funny: before the
        // UrT game code runs, you're actually on team 0 as you should be for FFA;
        // the UrT game code forces you back on your old team because it's insane;
        // then we force it back (if we have to) in the kludge; wow :-/ [mad]
        SV_UrT_FreeForAll_Kludge(client);
}

/*
============================================================

CLIENT COMMAND EXECUTION

============================================================
*/

/*
==================
SV_CloseDownload

clear/free any download vars
==================
*/
static void SV_CloseDownload( client_t *cl ) {
	int i;

	// EOF
	if (cl->download) {
		FS_FCloseFile( cl->download );
	}
	cl->download = 0;
	*cl->downloadName = 0;

	// Free the temporary buffer space
	for (i = 0; i < MAX_DOWNLOAD_WINDOW; i++) {
		if (cl->downloadBlocks[i]) {
			Z_Free( cl->downloadBlocks[i] );
			cl->downloadBlocks[i] = NULL;
		}
	}

}

/*
========================
SVC_GetModSlotByPassword
Get the mod slot of a given mod password. 0 for failure.
========================
*/
int SVC_GetModSlotByPassword(char *password)
{
    int i;
    
    if (!password[0])
        return 0;
    
    for (i = 0; i < MAX_MOD_LEVELS; i++)
    {
        if (strcmp(password, sv_moderatorpass[i]->string) == 0)
            return i + 1;
    }
    
    return 0;
}

/*
=============
SV_ModLogin_f
=============
*/
void SV_ModLogin_f(client_t * cl)
{
    char *password;
    int slot;
    
    if (sv_moderatorenable->integer < 1)
    {
        SV_SendServerCommand(cl,
                             "print \"Moderator access is currently disabled on this server.\n\"");
        Com_Printf
        ("Unsuccessful moderator login from client %i, %s. Reason: Moderators disabled.\n",
         cl - svs.clients, cl->name);
        return;
    }
    
    password = Cmd_Argv(1);
    slot = SVC_GetModSlotByPassword(password);
    
    if (slot > 0)
    {
        cl->mod_slot = slot;    // Save that this user is logged in as a mod.
        
        SV_SendServerCommand(cl,
                             "print \"You are now logged in as a moderator in slot %i.\n\"",
                             cl->mod_slot);
        SV_SendServerCommand(cl,
                             "print \"You may now execute the following commands:\n\"");
        SV_SendServerCommand(cl, "print \"%s\n\"",
                             sv_moderatorcommands[cl->mod_slot - 1]->string);
        Com_Printf
        ("Successful moderator login from client %i, %s. Mod slot: %i. Password: %s\n",
         cl - svs.clients, cl->name, cl->mod_slot, password);
    }
    else
    {
        SV_SendServerCommand(cl, "print \"^7Invalid moderator password.\n\"");
        Com_Printf
        ("Unsuccessful moderator login from client %i, %s. Reason: Invalid password. Password: %s\n",
         cl - svs.clients, cl->name, password);
    }
}

/*
==============
SV_ModLogout_f
==============
*/
void SV_ModLogout_f(client_t * cl)
{
    if (cl->mod_slot > 0)
    {
        cl->mod_slot = 0;
        
        SV_SendServerCommand(cl, "print \"You are no longer a moderator.\n\"");
        Com_Printf("Moderator logout issued from client %i, %s.\n",
                   cl - svs.clients, cl->name);
    }
    else
        SV_SendServerCommand(cl,
                             "print \"You are not currently a moderator.\n\"");
}

/*
====================
SV_ModCommandAllowed

This command will tell whether the command given in the second argument is
contained in a comma AND/OR space seperated list of allowed commands.

For example: "say" is allowed by "kick, mute, say, map"
but the command "status" is not.
====================
*/
qboolean SV_ModCommandAllowed(char *allowed, char *command)
{
    qboolean failed = qfalse;
    char *tmp = allowed;
    int cmd_offset = 0;
    int cmd_len = strlen(command);
    
    if (!cmd_len)
        return qfalse;
    
    for (; *tmp; tmp++)
    {
        if (*tmp == ',' || *tmp == ' ')
        {
            if (!failed && cmd_offset == cmd_len)
                return qtrue;
            
            failed = qfalse;
            cmd_offset = 0;
            continue;
        }
        
        if (failed)
            continue;
        
        if (cmd_offset == cmd_len)
            failed = qtrue;
        
        if (command[cmd_offset++] != *tmp)
            failed = qtrue;
    }
    
    if (!failed && cmd_offset == cmd_len)
        return qtrue;
    
    return qfalse;
}

/*
===================
SV_ModFlushRedirect
This function serves as a callback for the Com_BeginRedirect function.
It allows all command data to be printed out to a client's console. where
the client is defined by their netaddr stored in: svs.redirectAddress.
===================
*/
void SV_ModFlushRedirect(char *outputbuf)
{
    NET_OutOfBandPrint(NS_SERVER, svs.redirectAddress, "print\n%s", outputbuf);
}

/*
==================
SV_ModCommand_f
==================
*/
void SV_ModCommand_f(client_t * cl)
{
    char *cmd_ptr;
    char cmdstr[1024];
    int i;
    
#define SV_MOD_OUTPUTBUF_LENGTH (1024 - 16)
    char sv_mod_outputbuf[SV_MOD_OUTPUTBUF_LENGTH]; // A place to hold data before we dump it to the client's console.
    
    if (sv_moderatorenable->integer < 1)
    {
        SV_SendServerCommand(cl,
                             "print \"Moderator access is currently disabled on this server.\n\"");
        Com_Printf
        ("Moderator command rejected from client %i, %s. Reason: Moderators disabled.\n",
         cl - svs.clients, cl->name);
        return;
    }
    
    if (!cl->mod_slot)
    {
        SV_SendServerCommand(cl,
                             "print \"You are not logged in as a moderator.\n\"");
        Com_Printf
        ("Moderator command rejected from client %i, %s. Reason: Not logged in.\n",
         cl - svs.clients, cl->name);
        return;
    }
    
    if (Cmd_Argc() < 2)
    {
        SV_SendServerCommand(cl, "print \"No moderator command specified.\n\"");
        SV_SendServerCommand(cl, "print \"Usage: %s <command> [arguments]\n\"",
                             Cmd_Argv(0));
        Com_Printf
        ("Moderator command rejected from client %i, %s. Reason: Empty command string.\n",
         cl - svs.clients, cl->name);
        return;
    }
    
    if (!SV_ModCommandAllowed(sv_moderatorcommands[cl->mod_slot - 1]->string,
                              Cmd_Argv(1)))
    {
        Com_Printf
        ("Moderator command rejected from client %i, %s. Reason: Command not allowed for slot: %i.\n",
         cl - svs.clients, cl->name, cl->mod_slot);
        SV_SendServerCommand(cl,
                             "print \"You are not permitted to execute that command under moderator slot: %i.\n\"",
                             cl->mod_slot);
        return;
    }
    
    // To deal with quoting, we should use the full comand, skipping until we find the full command line to exec.
    cmd_ptr = Cmd_Cmd();
    
    while (cmd_ptr[0] == ' ')   // Trim off more leading spaces.
        cmd_ptr++;
    while (cmd_ptr[0] != '\0' && cmd_ptr[0] != ' ') // Trim off main command (arg0).
        cmd_ptr++;
    while (cmd_ptr[0] == ' ')   // Trim off spaces after arg0.
        cmd_ptr++;
    
    // Copy the full command to a new string for trimming and execution.
    cmdstr[0] = '\0';
    Q_strcat(cmdstr, sizeof(cmdstr), cmd_ptr);
    
    // Make sure nobody tried to sneak in a second command into the args with a command separator
    for (i = 0; cmdstr[i]; i++)
    {
        if (cmdstr[i] == ',' || cmdstr[i] == '\n' || cmdstr[i] == '\r' || (cmdstr[i] == '\\' && cmdstr[i + 1] == '$'))  // no cvar substitution (eg \$rconpassword\)
        {
            Com_Printf
            ("Moderator command rejected from client %i, %s. Reason: Command contains seperators.\n",
             cl - svs.clients, cl->name);
            SV_SendServerCommand(cl,
                                 "print \"Moderator command contains a disallowed character.\n\"");
            return;
        }
    }
    
    Com_Printf("Moderator command accepted from %i, %s. Command: %s\n",
               cl - svs.clients, cl->name, cmdstr);
    
    // Start redirecting all print outputs to the the client that issued this mod command.
    svs.redirectAddress = cl->netchan.remoteAddress;
    Com_BeginRedirect(sv_mod_outputbuf, SV_MOD_OUTPUTBUF_LENGTH,
                      SV_ModFlushRedirect);
    Cmd_ExecuteString(cmdstr);
    Com_EndRedirect();
}


/*
==================
SV_StopDownload_f

Abort a download if in progress
==================
*/
void SV_StopDownload_f( client_t *cl ) {
	if (*cl->downloadName)
		Com_DPrintf( "clientDownload: %d : file \"%s\" aborted\n", (int) (cl - svs.clients), cl->downloadName );

	SV_CloseDownload( cl );
}

/*
==================
SV_DoneDownload_f

Downloads are finished
==================
*/
void SV_DoneDownload_f( client_t *cl ) {
	Com_DPrintf( "clientDownload: %s Done\n", cl->name);
	// resend the game state to update any clients that entered during the download
	SV_SendClientGameState(cl);
}

/*
==================
SV_NextDownload_f

The argument will be the last acknowledged block from the client, it should be
the same as cl->downloadClientBlock
==================
*/
void SV_NextDownload_f( client_t *cl )
{
	int block = atoi( Cmd_Argv(1) );

	if (block == cl->downloadClientBlock) {
		Com_DPrintf( "clientDownload: %d : client acknowledge of block %d\n", (int) (cl - svs.clients), block );

		// Find out if we are done.  A zero-length block indicates EOF
		if (cl->downloadBlockSize[cl->downloadClientBlock % MAX_DOWNLOAD_WINDOW] == 0) {
			Com_Printf( "clientDownload: %d : file \"%s\" completed\n", (int) (cl - svs.clients), cl->downloadName );
			SV_CloseDownload( cl );
			return;
		}

		cl->downloadSendTime = svs.time;
		cl->downloadClientBlock++;
		return;
	}
	// We aren't getting an acknowledge for the correct block, drop the client
	// FIXME: this is bad... the client will never parse the disconnect message
	//			because the cgame isn't loaded yet
	SV_DropClient( cl, "broken download" );
}

/*
==================
SV_BeginDownload_f
==================
*/
void SV_BeginDownload_f( client_t *cl ) {

	// Kill any existing download
	SV_CloseDownload( cl );

	// cl->downloadName is non-zero now, SV_WriteDownloadToClient will see this and open
	// the file itself
	Q_strncpyz( cl->downloadName, Cmd_Argv(1), sizeof(cl->downloadName) );
}

/*
==================
SV_WriteDownloadToClient

Check to see if the client wants a file, open it if needed and start pumping the client
Fill up msg with data 
==================
*/
void SV_WriteDownloadToClient( client_t *cl , msg_t *msg )
{
	int curindex;
	int rate;
	int blockspersnap;
	int idPack = 0, missionPack = 0, unreferenced = 1;
	char errorMessage[1024];
	char pakbuf[MAX_QPATH], *pakptr;
	int numRefPaks;

	if (!*cl->downloadName)
		return;	// Nothing being downloaded

	if (!cl->download) {
 		// Chop off filename extension.
		Com_sprintf(pakbuf, sizeof(pakbuf), "%s", cl->downloadName);
		pakptr = Q_strrchr(pakbuf, '.');
		
		if(pakptr)
		{
			*pakptr = '\0';

			// Check for pk3 filename extension
			if(!Q_stricmp(pakptr + 1, "pk3"))
			{
				const char *referencedPaks = FS_ReferencedPakNames();

				// Check whether the file appears in the list of referenced
				// paks to prevent downloading of arbitrary files.
				Cmd_TokenizeStringIgnoreQuotes(referencedPaks);
				numRefPaks = Cmd_Argc();

				for(curindex = 0; curindex < numRefPaks; curindex++)
				{
					if(!FS_FilenameCompare(Cmd_Argv(curindex), pakbuf))
					{
						unreferenced = 0;

						// now that we know the file is referenced,
						// check whether it's legal to download it.
						missionPack = FS_idPak(pakbuf, "missionpack");
						idPack = missionPack || FS_idPak(pakbuf, BASEGAME);

						break;
					}
				}
			}
		}

		cl->download = 0;

		// We open the file here
		if ( !(sv_allowDownload->integer & DLF_ENABLE) ||
			(sv_allowDownload->integer & DLF_NO_UDP) ||
			idPack || unreferenced ||
			( cl->downloadSize = FS_SV_FOpenFileRead( cl->downloadName, &cl->download ) ) < 0 ) {
			// cannot auto-download file
			if(unreferenced)
			{
				Com_Printf("clientDownload: %d : \"%s\" is not referenced and cannot be downloaded.\n", (int) (cl - svs.clients), cl->downloadName);
				Com_sprintf(errorMessage, sizeof(errorMessage), "File \"%s\" is not referenced and cannot be downloaded.", cl->downloadName);
			}
			else if (idPack) {
				Com_Printf("clientDownload: %d : \"%s\" cannot download id pk3 files\n", (int) (cl - svs.clients), cl->downloadName);
				if (missionPack) {
					Com_sprintf(errorMessage, sizeof(errorMessage), "Cannot autodownload Team Arena file \"%s\"\n"
									"The Team Arena mission pack can be found in your local game store.", cl->downloadName);
				}
				else {
					Com_sprintf(errorMessage, sizeof(errorMessage), "Cannot autodownload id pk3 file \"%s\"", cl->downloadName);
				}
			}
			else if ( !(sv_allowDownload->integer & DLF_ENABLE) ||
				(sv_allowDownload->integer & DLF_NO_UDP) ) {

				Com_Printf("clientDownload: %d : \"%s\" download disabled", (int) (cl - svs.clients), cl->downloadName);
				if (sv_pure->integer) {
					Com_sprintf(errorMessage, sizeof(errorMessage), "Could not download \"%s\" because autodownloading is disabled on the server.\n\n"
										"You will need to get this file elsewhere before you "
										"can connect to this pure server.\n", cl->downloadName);
				} else {
					Com_sprintf(errorMessage, sizeof(errorMessage), "Could not download \"%s\" because autodownloading is disabled on the server.\n\n"
                    "The server you are connecting to is not a pure server, "
                    "set autodownload to No in your settings and you might be "
                    "able to join the game anyway.\n", cl->downloadName);
				}
			} else {
        // NOTE TTimo this is NOT supposed to happen unless bug in our filesystem scheme?
        //   if the pk3 is referenced, it must have been found somewhere in the filesystem
				Com_Printf("clientDownload: %d : \"%s\" file not found on server\n", (int) (cl - svs.clients), cl->downloadName);
				Com_sprintf(errorMessage, sizeof(errorMessage), "File \"%s\" not found on server for autodownloading.\n", cl->downloadName);
			}
			MSG_WriteByte( msg, svc_download );
			MSG_WriteShort( msg, 0 ); // client is expecting block zero
			MSG_WriteLong( msg, -1 ); // illegal file size
			MSG_WriteString( msg, errorMessage );

			*cl->downloadName = 0;
			
			if(cl->download)
				FS_FCloseFile(cl->download);
			
			return;
		}
 
		Com_Printf( "clientDownload: %d : beginning \"%s\"\n", (int) (cl - svs.clients), cl->downloadName );
		
		// Init
		cl->downloadCurrentBlock = cl->downloadClientBlock = cl->downloadXmitBlock = 0;
		cl->downloadCount = 0;
		cl->downloadEOF = qfalse;
	}

	// Perform any reads that we need to
	while (cl->downloadCurrentBlock - cl->downloadClientBlock < MAX_DOWNLOAD_WINDOW &&
		cl->downloadSize != cl->downloadCount) {

		curindex = (cl->downloadCurrentBlock % MAX_DOWNLOAD_WINDOW);

		if (!cl->downloadBlocks[curindex])
			cl->downloadBlocks[curindex] = Z_Malloc( MAX_DOWNLOAD_BLKSIZE );

		cl->downloadBlockSize[curindex] = FS_Read( cl->downloadBlocks[curindex], MAX_DOWNLOAD_BLKSIZE, cl->download );

		if (cl->downloadBlockSize[curindex] < 0) {
			// EOF right now
			cl->downloadCount = cl->downloadSize;
			break;
		}

		cl->downloadCount += cl->downloadBlockSize[curindex];

		// Load in next block
		cl->downloadCurrentBlock++;
	}

	// Check to see if we have eof condition and add the EOF block
	if (cl->downloadCount == cl->downloadSize &&
		!cl->downloadEOF &&
		cl->downloadCurrentBlock - cl->downloadClientBlock < MAX_DOWNLOAD_WINDOW) {

		cl->downloadBlockSize[cl->downloadCurrentBlock % MAX_DOWNLOAD_WINDOW] = 0;
		cl->downloadCurrentBlock++;

		cl->downloadEOF = qtrue;  // We have added the EOF block
	}

	// Loop up to window size times based on how many blocks we can fit in the
	// client snapMsec and rate

	// based on the rate, how many bytes can we fit in the snapMsec time of the client
	// normal rate / snapshotMsec calculation
	rate = cl->rate;
	if ( sv_maxRate->integer ) {
		if ( sv_maxRate->integer < 1000 ) {
			Cvar_Set( "sv_MaxRate", "1000" );
		}
		if ( sv_maxRate->integer < rate ) {
			rate = sv_maxRate->integer;
		}
	}
	if ( sv_minRate->integer ) {
		if ( sv_minRate->integer < 1000 )
			Cvar_Set( "sv_minRate", "1000" );
		if ( sv_minRate->integer > rate )
			rate = sv_minRate->integer;
	}

	if (!rate) {
		blockspersnap = 1;
	} else {
		blockspersnap = ( (rate * cl->snapshotMsec) / 1000 + MAX_DOWNLOAD_BLKSIZE ) /
			MAX_DOWNLOAD_BLKSIZE;
	}

	if (blockspersnap < 0)
		blockspersnap = 1;

	while (blockspersnap--) {

		// Write out the next section of the file, if we have already reached our window,
		// automatically start retransmitting

		if (cl->downloadClientBlock == cl->downloadCurrentBlock)
			return; // Nothing to transmit

		if (cl->downloadXmitBlock == cl->downloadCurrentBlock) {
			// We have transmitted the complete window, should we start resending?

			//FIXME:  This uses a hardcoded one second timeout for lost blocks
			//the timeout should be based on client rate somehow
			if (svs.time - cl->downloadSendTime > 1000)
				cl->downloadXmitBlock = cl->downloadClientBlock;
			else
				return;
		}

		// Send current block
		curindex = (cl->downloadXmitBlock % MAX_DOWNLOAD_WINDOW);

		MSG_WriteByte( msg, svc_download );
		MSG_WriteShort( msg, cl->downloadXmitBlock );

		// block zero is special, contains file size
		if ( cl->downloadXmitBlock == 0 )
			MSG_WriteLong( msg, cl->downloadSize );
 
		MSG_WriteShort( msg, cl->downloadBlockSize[curindex] );

		// Write the block
		if ( cl->downloadBlockSize[curindex] ) {
			MSG_WriteData( msg, cl->downloadBlocks[curindex], cl->downloadBlockSize[curindex] );
		}

		Com_DPrintf( "clientDownload: %d : writing block %d\n", (int) (cl - svs.clients), cl->downloadXmitBlock );

		// Move on to the next block
		// It will get sent with next snap shot.  The rate will keep us in line.
		cl->downloadXmitBlock++;

		cl->downloadSendTime = svs.time;
	}
}

#ifdef USE_VOIP
/*
==================
SV_WriteVoipToClient

Check to see if there is any VoIP queued for a client, and send if there is.
==================
*/
void SV_WriteVoipToClient( client_t *cl, msg_t *msg )
{
	voipServerPacket_t *packet = &cl->voipPacket[0];
	int totalbytes = 0;
	int i;

	if (*cl->downloadName) {
		cl->queuedVoipPackets = 0;
		return;  // no VoIP allowed if download is going, to save bandwidth.
	}

	// Write as many VoIP packets as we reasonably can...
	for (i = 0; i < cl->queuedVoipPackets; i++, packet++) {
		totalbytes += packet->len;
		if (totalbytes > MAX_DOWNLOAD_BLKSIZE)
			break;

		// You have to start with a svc_EOF, so legacy clients drop the
		//  rest of this packet. Otherwise, those without VoIP support will
		//  see the svc_voip command, then panic and disconnect.
		// Generally we don't send VoIP packets to legacy clients, but this
		//  serves as both a safety measure and a means to keep demo files
		//  compatible.
		MSG_WriteByte( msg, svc_EOF );
		MSG_WriteByte( msg, svc_extension );
		MSG_WriteByte( msg, svc_voip );
		MSG_WriteShort( msg, packet->sender );
		MSG_WriteByte( msg, (byte) packet->generation );
		MSG_WriteLong( msg, packet->sequence );
		MSG_WriteByte( msg, packet->frames );
		MSG_WriteShort( msg, packet->len );
		MSG_WriteData( msg, packet->data, packet->len );
	}

	// !!! FIXME: I hate this queue system.
	cl->queuedVoipPackets -= i;
	if (cl->queuedVoipPackets > 0) {
		memmove( &cl->voipPacket[0], &cl->voipPacket[i],
		         sizeof (voipServerPacket_t) * i);
	}
}
#endif


/*
=================
SV_Disconnect_f

The client is going to disconnect, so remove the connection immediately  FIXME: move to game?
=================
*/
static void SV_Disconnect_f( client_t *cl ) {
	SV_DropClient( cl, "disconnected" );
}

/*
=================
SV_VerifyPaks_f

If we are pure, disconnect the client if they do no meet the following conditions:

1. the first two checksums match our view of cgame and ui
2. there are no any additional checksums that we do not have

This routine would be a bit simpler with a goto but i abstained

=================
*/
static void SV_VerifyPaks_f( client_t *cl ) {
	int nChkSum1, nChkSum2, nClientPaks, nServerPaks, i, j, nCurArg;
	int nClientChkSum[1024];
	int nServerChkSum[1024];
	const char *pPaks, *pArg;
	qboolean bGood = qtrue;

	// if we are pure, we "expect" the client to load certain things from 
	// certain pk3 files, namely we want the client to have loaded the
	// ui and cgame that we think should be loaded based on the pure setting
	//
	if ( sv_pure->integer != 0 ) {

		bGood = qtrue;
		nChkSum1 = nChkSum2 = 0;
		// we run the game, so determine which cgame and ui the client "should" be running
		bGood = (FS_FileIsInPAK("vm/cgame.qvm", &nChkSum1) == 1);
		if (bGood)
			bGood = (FS_FileIsInPAK("vm/ui.qvm", &nChkSum2) == 1);

		nClientPaks = Cmd_Argc();

		// start at arg 2 ( skip serverId cl_paks )
		nCurArg = 1;

		pArg = Cmd_Argv(nCurArg++);
		if(!pArg) {
			bGood = qfalse;
		}
		else
		{
			// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=475
			// we may get incoming cp sequences from a previous checksumFeed, which we need to ignore
			// since serverId is a frame count, it always goes up
			if (atoi(pArg) < sv.checksumFeedServerId)
			{
				Com_DPrintf("ignoring outdated cp command from client %s\n", cl->name);
				return;
			}
		}
	
		// we basically use this while loop to avoid using 'goto' :)
		while (bGood) {

			// must be at least 6: "cl_paks cgame ui @ firstref ... numChecksums"
			// numChecksums is encoded
			if (nClientPaks < 6) {
				bGood = qfalse;
				break;
			}
			// verify first to be the cgame checksum
			pArg = Cmd_Argv(nCurArg++);
			if (!pArg || *pArg == '@' || atoi(pArg) != nChkSum1 ) {
				bGood = qfalse;
				break;
			}
			// verify the second to be the ui checksum
			pArg = Cmd_Argv(nCurArg++);
			if (!pArg || *pArg == '@' || atoi(pArg) != nChkSum2 ) {
				bGood = qfalse;
				break;
			}
			// should be sitting at the delimeter now
			pArg = Cmd_Argv(nCurArg++);
			if (*pArg != '@') {
				bGood = qfalse;
				break;
			}
			// store checksums since tokenization is not re-entrant
			for (i = 0; nCurArg < nClientPaks; i++) {
				nClientChkSum[i] = atoi(Cmd_Argv(nCurArg++));
			}

			// store number to compare against (minus one cause the last is the number of checksums)
			nClientPaks = i - 1;

			// make sure none of the client check sums are the same
			// so the client can't send 5 the same checksums
			for (i = 0; i < nClientPaks; i++) {
				for (j = 0; j < nClientPaks; j++) {
					if (i == j)
						continue;
					if (nClientChkSum[i] == nClientChkSum[j]) {
						bGood = qfalse;
						break;
					}
				}
				if (bGood == qfalse)
					break;
			}
			if (bGood == qfalse)
				break;

			// get the pure checksums of the pk3 files loaded by the server
			pPaks = FS_LoadedPakPureChecksums();
			Cmd_TokenizeString( pPaks );
			nServerPaks = Cmd_Argc();
			if (nServerPaks > 1024)
				nServerPaks = 1024;

			for (i = 0; i < nServerPaks; i++) {
				nServerChkSum[i] = atoi(Cmd_Argv(i));
			}

			// check if the client has provided any pure checksums of pk3 files not loaded by the server
			for (i = 0; i < nClientPaks; i++) {
				for (j = 0; j < nServerPaks; j++) {
					if (nClientChkSum[i] == nServerChkSum[j]) {
						break;
					}
				}
				if (j >= nServerPaks) {
					bGood = qfalse;
					break;
				}
			}
			if ( bGood == qfalse ) {
				break;
			}

			// check if the number of checksums was correct
			nChkSum1 = sv.checksumFeed;
			for (i = 0; i < nClientPaks; i++) {
				nChkSum1 ^= nClientChkSum[i];
			}
			nChkSum1 ^= nClientPaks;
			if (nChkSum1 != nClientChkSum[nClientPaks]) {
				bGood = qfalse;
				break;
			}

			// break out
			break;
		}

		cl->gotCP = qtrue;

		if (bGood) {
			cl->pureAuthentic = 1;
		} 
		else {
			cl->pureAuthentic = 0;
			cl->nextSnapshotTime = -1;
			cl->state = CS_ACTIVE;
			SV_SendClientSnapshot( cl );
			SV_DropClient( cl, "Unpure client detected. Invalid .PK3 files referenced!" );
		}
	}
}

/*
=================
SV_ResetPureClient_f
=================
*/
static void SV_ResetPureClient_f( client_t *cl ) {
	cl->pureAuthentic = 0;
	cl->gotCP = qfalse;
}

/*
=================
SV_UserinfoChanged

Pull specific info from a newly changed userinfo string
into a more C friendly form.

The return value is the empty string ("") if the userinfo was successfully changed.
The return string is non-empty if there is a problem such as a userinfo overflow.
In that case, the returned string is the nature of the error.  If the client
is not yet connected (e.g. in SV_DirectConnect()), the returned error message should
be sent in a NET_OutOfBandPrint().  If this is being called from SV_UpdateUserinfo_f(),
the SV_DropClient() call will take care of a proper error message to the client,
because the state of this client will be CS_CONNECTED or greater, and SV_DropClient()
will send the appropriate server command to the client with the reason why they were
dropped.
=================
*/
char *SV_UserinfoChanged( client_t *cl ) {
	char	*val;
	char	*ip;
	int		i;

	if (!cl->location[0]) {
		Info_RemoveKey(cl->userinfo, "location");
	}
	////////////////////////////////////////////////////
	// separator for ip2loc.patch and namesanitize.patch
	////////////////////////////////////////////////////

	int nameSanitizeRtn = SV_SanitizeNameConditionally(cl->userinfo);
	if (nameSanitizeRtn > 0) {
		SV_DropClient(cl, "userinfo string length exceeded");
		return "Userinfo overflow.";
	}
	else if (nameSanitizeRtn < 0) {
		SV_DropClient(cl, "illegal characters in player name");
		Com_DPrintf("Illegal chars in player name \"%s\" for client #%i\n",
				Info_ValueForKey(cl->userinfo, "name"), (int) (cl - svs.clients));
		return "Illegal characters in player name.";
	}
	// name for C code
	Q_strncpyz( cl->name, Info_ValueForKey (cl->userinfo, "name"), sizeof(cl->name) );

	// rate command

	// if the client is on the same subnet as the server and we aren't running an
	// internet public server, assume they don't need a rate choke
	if ( Sys_IsLANAddress( cl->netchan.remoteAddress ) && com_dedicated->integer != 2 && sv_lanForceRate->integer == 1) {
		cl->rate = 90000;	// lans should not rate limit
	} else {
		val = Info_ValueForKey (cl->userinfo, "rate");
		if (strlen(val)) {
			i = atoi(val);
			cl->rate = i;
			if (cl->rate < 1000) {
				cl->rate = 1000;
			} else if (cl->rate > 90000) {
				cl->rate = 90000;
			}
		} else {
			cl->rate = 3000;
		}
	}
	val = Info_ValueForKey (cl->userinfo, "handicap");
	if (strlen(val)) {
		i = atoi(val);
		if (i<=0 || i>100 || strlen(val) > 4) {
			if (Info_SetValueForKeySilent(cl->userinfo, "handicap", "100")) {
				SV_DropClient(cl, "userinfo string length exceeded");
				return "Userinfo overflow.";
			}
		}
	}

	// snaps command
	val = Info_ValueForKey (cl->userinfo, "snaps");
	if (strlen(val)) {
		i = atoi(val);
		if ( i < 1 ) {
			i = 1;
		} else if ( i > sv_fps->integer ) {
			i = sv_fps->integer;
		}
		cl->snapshotMsec = 1000/i;
	} else {
		cl->snapshotMsec = 50;
	}
	
#ifdef USE_VOIP
	// in the future, (val) will be a protocol version string, so only
	//  accept explicitly 1, not generally non-zero.
	val = Info_ValueForKey (cl->userinfo, "cl_voip");
	cl->hasVoip = (atoi(val) == 1) ? qtrue : qfalse;
#endif

	if (!SV_ConditionalNoKevlar(cl->userinfo)) {
		SV_DropClient(cl, "userinfo string length exceeded");
		return "Userinfo overflow.";
	}          

	// TTimo
	// maintain the IP information
	// the banning code relies on this being consistently present
	if( NET_IsLocalAddress(cl->netchan.remoteAddress) )
		ip = "localhost";
	else
		ip = (char*) NET_AdrToStringwPort(cl->netchan.remoteAddress);
	if (Info_SetValueForKeySilent(cl->userinfo, "ip", ip)) {
		SV_DropClient( cl, "userinfo string length exceeded" );
		return "Userinfo overflow.";
	}

	// Don't allow the client to override their location cvar.
	if (cl->location[0]) {
		// We've already made sure that the cl->location string does not contain illegal characters.
		if (Info_SetValueForKeySilent(cl->userinfo, "location", cl->location)) {
			SV_DropClient(cl, "userinfo string length exceeded");
			return "Userinfo overflow.";
		}
	}
	////////////////////////////////////////////////
	// separator for ip2loc.patch and playerdb.patch
	////////////////////////////////////////////////
	if (!(cl->netchan.remoteAddress.type == NA_BOT || Sys_IsLANAddress(cl->netchan.remoteAddress))) {
		// SV_ApproveGuid already checks for sv_requireValidGuid.
		if (!SV_ApproveGuid(Info_ValueForKey(cl->userinfo, "cl_guid"))) {
			SV_DropClient(cl, "not legit, bro");
			return "Get Legit, bro.";
		}
		SV_SendUserinfoToPlayerDBConditionally(cl->userinfo);
	}

	return "";
}


/*
==================
SV_UpdateUserinfo_f
==================
*/
static void SV_UpdateUserinfo_f( client_t *cl ) {

	Com_DPrintf("SV_UpdateUserinfo_f for client #%i, state %i\n", (int) (cl - svs.clients), cl->state);

	// This function will never be called for bots.
	if (sv_userinfoFloodProtect->integer) {
		Com_DPrintf("    ...lastUserinfoChangeTime is %i\n", cl->lastUserinfoChangeTime);
		if (svs.time - cl->lastUserinfoChangeTime < 2000) {
			// Allow 2 userinfo changes every 2 seconds or so.
			if ((cl->lastUserinfoChangeTime & 0x01) == 0) {
				cl->lastUserinfoChangeTime |= 0x01;
			}
			else {
				// This is now at least our second userinfo change in a period of 2 seconds.  Deny it.
				if ((cl->lastUserinfoChangeTime & 0x02) == 0) {
					SV_SendServerCommand(cl, "print \"You cannot change your userinfo that quickly.\n\"");
					cl->lastUserinfoChangeTime |= 0x02;
				}
				return;
			}
		}
		else {
			cl->lastUserinfoChangeTime = (svs.time & (~0x03)); // Leave 2 low bits for special meaning.
		}
	}

	// Cmd_Argv(1) actually cannot overflow cl->userinfo.  Top of SV_ClientCommand()
	// calls MSG_ReadString() which returns a string in a buffer of size MAX_STRING_CHARS,
	// and that is currently the same as MAX_INFO_STRING.
	Q_strncpyz( cl->userinfo, Cmd_Argv(1), sizeof(cl->userinfo) );
	if (!Info_Validate(cl->userinfo)) {
		SV_DropClient(cl, "illegal characters in userinfo");
		return;
	}

	char *userinfoChangedRtn = SV_UserinfoChanged(cl);
	if (userinfoChangedRtn && userinfoChangedRtn[0]) { return; }
	// call prog code to allow overrides
	VM_Call( gvm, GAME_CLIENT_USERINFO_CHANGED, cl - svs.clients );
}

#ifdef USE_VOIP
static
void SV_UpdateVoipIgnore(client_t *cl, const char *idstr, qboolean ignore)
{
	if ((*idstr >= '0') && (*idstr <= '9')) {
		const int id = atoi(idstr);
		if ((id >= 0) && (id < MAX_CLIENTS)) {
			cl->ignoreVoipFromClient[id] = ignore;
		}
	}
}

/*
==================
SV_UpdateUserinfo_f
==================
*/
static void SV_Voip_f( client_t *cl ) {
	const char *cmd = Cmd_Argv(1);
	if (strcmp(cmd, "ignore") == 0) {
		SV_UpdateVoipIgnore(cl, Cmd_Argv(2), qtrue);
	} else if (strcmp(cmd, "unignore") == 0) {
		SV_UpdateVoipIgnore(cl, Cmd_Argv(2), qfalse);
	} else if (strcmp(cmd, "muteall") == 0) {
		cl->muteAllVoip = qtrue;
	} else if (strcmp(cmd, "unmuteall") == 0) {
		cl->muteAllVoip = qfalse;
	}
}
#endif

/*
====================
SV_ReadUserLocations
====================
*/
void SV_ReadUserLocations(void)
{
    int filelen, tmp;
    char *curpos;
    fileHandle_t readfrom;
    char filepath[MAX_QPATH];

    userLocCount = 0;

    if (!(curpos = Cvar_VariableString("fs_game")) || !*curpos)
        curpos = BASEGAME;

    Com_sprintf(filepath, sizeof(filepath), "%s/%s", curpos, "userlocs.dat");

    if ((filelen = FS_SV_FOpenFileRead(filepath, &readfrom)) >= 0)
    {
        if (filelen < sizeof(int))
            goto invalid;

        FS_Read(&tmp, sizeof(int), readfrom);

        if (tmp < 0 || tmp > sizeof(userLocs) ||
            filelen != (sizeof(int) + tmp * sizeof(userLoc_t)))
            goto invalid;

        userLocCount = tmp;

        FS_Read(userLocs, sizeof(userLoc_t) * userLocCount, readfrom);
        FS_FCloseFile(readfrom);
    }

    return;

  invalid:
    Com_Printf("userlocs.dat file is invalid\n");
    FS_FCloseFile(readfrom);
}


/*
=====================
SV_WriteUserLocations
=====================
*/
void SV_WriteUserLocations(void)
{
    char *curpos;
    fileHandle_t writeto;
    char filepath[MAX_QPATH];

    if (!(curpos = Cvar_VariableString("fs_game")) || !*curpos)
        curpos = BASEGAME;

    Com_sprintf(filepath, sizeof(filepath), "%s/%s", curpos, "userlocs.dat");

    if ((writeto = FS_SV_FOpenFileWrite(filepath)))
    {
        FS_Write(&userLocCount, sizeof(int), writeto);
        FS_Write(userLocs, sizeof(userLoc_t) * userLocCount, writeto);
        FS_FCloseFile(writeto);
    }
}

/*
==================
SV_DisplayGotoHelp_f
==================
*/
static void SV_DisplayGotoHelp_f(client_t *cl) {
	SV_SendServerCommand(cl, "print \"^3The following are commands for save/load position and goto:\n\"");
	SV_SendServerCommand(cl, "print \"    ^3\\help   ^1-> ^7Show this list of commands\n\"");
	SV_SendServerCommand(cl, "print \"    ^3\\save   ^1-> ^7Save current position\n\"");
	SV_SendServerCommand(cl, "print \"    ^3\\load   ^1-> ^7Load saved position%s\n\"",
			(sv_allowLoadPosition->integer > 0) ? "" : " (currently disabled)");
	SV_SendServerCommand(cl, "print \"    ^3\\allowgoto   ^1 -> ^7Allow players to teleport where you are\n\"");
	SV_SendServerCommand(cl, "print \"    ^3\\goto ^4<client> ^1 ->^7 Goto another player%s\n\"",
			(sv_allowGoto->integer > 0) ? "" : " (currently disabled)");
}


/*
==================
SV_SavePosition_f
==================
*/
static void SV_SavePosition_f(client_t *cl) {
	int		clId;
	playerState_t	*myState;
	char *guid, *mapname;
	int i;

	if (!(sv_allowLoadPosition->integer > 0)) {
		SV_SendServerCommand(cl, "print \"^7Save is disabled on server.\n\"");
		return;
	}
	clId = cl - svs.clients;
	if (TEAM_SPECTATOR == atoi(Info_ValueForKey(sv.configstrings[548 + clId], "t"))) {
		SV_SendServerCommand(cl, "print \"^7You ^1can not be in spectators ^7when saving your position.\n\"");
		return;
	}
	myState = SV_GameClientNum(clId);
	if (myState->pm_type != PM_NORMAL) {
		SV_SendServerCommand(cl, "print \"^7You ^1must be alive and in-game ^7when saving your position.\n\"");
		return;
	}
	if (Cmd_Argc() > 1) {
		SV_SendServerCommand(cl, "print \"^1Too many arguments ^7to saveposition command, none expected.\n\"");
		return;
	}
	if (myState->groundEntityNum != ENTITYNUM_WORLD) {
		SV_SendServerCommand(cl, "print \"^7You ^1must be standing on solid ground ^7when saving your position.\n\"");
		return;
	}
	if (myState->velocity[0] != 0 || myState->velocity[1] != 0 || myState->velocity[2] != 0) {
		SV_SendServerCommand(cl, "print \"^7You ^1must be standing still ^7when saving your position.\n\"");
		return;
	}

	guid = Info_ValueForKey(cl->userinfo, "cl_guid");
	mapname = sv_mapname->string;

	// Find a save slot.
	for (i = 0; i < sizeof(userLocs); i++)
	{
		userLoc_t *p = userLocs + i;

		if (i < userLocCount
			&& (Q_stricmp(guid, p->guid) || Q_stricmp(mapname, p->map)))
			continue;

		Q_strncpyz(p->guid, guid, MAX_CVAR_VALUE_STRING);
		Q_strncpyz(p->map, mapname, MAX_QPATH);

		VectorCopy(myState->origin, p->pos);

		if (i == userLocCount)
		userLocCount++;

		break;
	}

	if (i == sizeof(userLocs))
	{
		SV_SendServerCommand(cl,
							"print \"" S_COLOR_RED
							"No more saving slots.\n\"");
		return;
	}

		SV_WriteUserLocations();

		cl->positionIsSaved = qtrue;
		SV_SendServerCommand(cl, "print \"^7You ^2saved ^7your position.\n\"");
}

/*
==================
SV_LoadPosition_f
==================
*/
static void SV_LoadPosition_f(client_t *cl)
{
	int		clId;
	playerState_t	*myState;
	char *guid, *mapname;
	int		i;
	int		loadPositionWaitTime, nextLoadPositionTime;

	if (!(sv_allowLoadPosition->integer > 0)) {
		SV_SendServerCommand(cl, "print \"^7Load is ^1disabled ^7on server.\n\"");
		return;
	}
	clId = cl - svs.clients;
	if (TEAM_SPECTATOR == atoi(Info_ValueForKey(sv.configstrings[548 + clId], "t"))) {
		SV_SendServerCommand(cl, "print \"^7You ^1can not be in spectators ^7when loading your position.\n\"");
		return;
	}
	myState = SV_GameClientNum(clId);
	if (myState->pm_type != PM_NORMAL) {
		SV_SendServerCommand(cl, "print \"^7You ^1must be alive^7 and in-game when loading your position.\n\"");
		return;
	}
	if (Cmd_Argc() > 1) {
		SV_SendServerCommand(cl, "print \"^1Too many arguments ^7to loadposition command, none expected.\n\"");
		return;
	}
	if (cl->lastLoadPositionTime > 0) { // Allow load position at "beginning of time" when svs.time is zero.
		loadPositionWaitTime = sv_loadPositionWaitTime->integer;
		if (loadPositionWaitTime < 0) { loadPositionWaitTime = 0; }
		else if (loadPositionWaitTime > 3600) { loadPositionWaitTime = 3600; }
		loadPositionWaitTime *= 1000;
		nextLoadPositionTime = cl->lastLoadPositionTime + loadPositionWaitTime;
		if (nextLoadPositionTime > svs.time) {
			SV_SendServerCommand(cl, "print \"^7You ^1must wait ^5%i ^7seconds before loading saved position again.\n\"",
					((nextLoadPositionTime - svs.time) / 1000) + 1);
			return;
		}
	}
	guid = Info_ValueForKey(cl->userinfo, "cl_guid");
	mapname = sv_mapname->string;

	for (i = 0; i < userLocCount; i++)
	{
		userLoc_t *p = userLocs + i;

		if (Q_stricmp(guid, p->guid) || Q_stricmp(mapname, p->map))
		continue;

		VectorCopy(p->pos, myState->origin);
		VectorClear(myState->velocity);
		cl->lastLoadPositionTime = svs.time;
		SV_SendServerCommand(cl, "print \"^7You ^5loaded ^7your position.\n\"");
		return;
	}

	SV_SendServerCommand(cl,
						"print \"" S_COLOR_RED
						"^7You have ^1not saved ^7your position on this map.\n\"");
}


/*
==================
SV_UserGetPlayerByHandle
==================
*/
static client_t *SV_UserGetPlayerByHandle(char *s) {
	client_t	*cl;
	int		i, plid;
	char		cleanName[64];

	if (!com_sv_running->integer) { return NULL; }

	for (i = 0; isdigit(s[i]); i++);
	
	if (!s[i]) {
		plid = atoi(s);
		if (plid >= 0 && plid < sv_maxclients->integer) {
			cl = &svs.clients[plid];
			if (cl->state) return cl;
		}
	}
	for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++) {
		if (!cl->state) { continue; }
		if (!Q_stricmp(cl->name, s)) { return cl; }
		Q_strncpyz(cleanName, cl->name, sizeof(cleanName));
		Q_CleanStr(cleanName);
		if (!Q_stricmp(cleanName, s)) { return cl; }
	}
	return NULL;
}


/*
==================
SV_UserAllowGoto_f
==================
*/
static void SV_UserAllowGoto_f(client_t *cl) {
    while (qtrue) { // Provides break structure.
		if (Cmd_Argc() != 2) { break; }
		if (strlen(Cmd_Argv(1)) != 1) { break; }
		if (Cmd_Argv(1)[0] == '0') {
			cl->allowGoto = qfalse;
			SV_SendServerCommand(cl, "print \"^7Players now are ^1not ^7allowed to ^6goto ^7to you.\n\"");
			return;
		}
		if (Cmd_Argv(1)[0] == '1') {
			cl->allowGoto = qtrue;
			SV_SendServerCommand(cl, "print \"^7Players now are ^1allowed ^7to ^6goto ^7to you.\n\"");
			return;
		}
		break;
	}
	cl->allowGoto = !cl->allowGoto;
	SV_SendServerCommand(cl, "print \"^7Players now are %s^7 to ^6goto ^7to you.\n\"",
				cl->allowGoto ? "^2allowed" : "^1not allowed");
	if (cl->allowGoto) {
		SV_SendServerCommand(cl, "print \" \n\"");
	}
	else {
		SV_SendServerCommand(cl, "print \"  \n\"");
	}
}


/*
==================
SV_Goto_f
==================
*/
static void SV_Goto_f(client_t *cl) {
	playerState_t	*myState;
	int		myClId, targetClId;
	int		gotoWaitTime, nextGotoTime;
	client_t	*targetCl;
	playerState_t	*targetState;

	if (!(sv_allowGoto->integer > 0)) {
		SV_SendServerCommand(cl, "print \"^7Goto is ^1disabled ^7on server.\n\"");
		return;
	}
	myClId = cl - svs.clients;
	if (TEAM_SPECTATOR == atoi(Info_ValueForKey(sv.configstrings[548 + myClId], "t"))) {
		SV_SendServerCommand(cl, "print \"^7You ^1cannot be in spectators ^7when using goto.\n\"");
		return;
	}
	myState = SV_GameClientNum(myClId);
	if (myState->pm_type != PM_NORMAL) {
		SV_SendServerCommand(cl, "print \"^7You ^1must be alive^7 and in-game when using goto.\n\"");
		return;
	}
	if (Cmd_Argc() > 2) {
		SV_SendServerCommand(cl, "print \"^1Too many arguments^7 in goto command.\n\"");
		return;
	}
	if (!(Cmd_Argv(1)[0])) {
		SV_SendServerCommand(cl, "print \"^7You ^1forgot to specify ^7a goto target client.\n\"");
		return;
	}
	if (cl->lastGotoTime > 0) { // Allow goto at "beginning of time" when svs.time is zero.
		gotoWaitTime = sv_gotoWaitTime->integer;
		if (gotoWaitTime < 0) { gotoWaitTime = 0; }
		else if (gotoWaitTime > 3600) { gotoWaitTime = 3600; }
		gotoWaitTime *= 1000;
		nextGotoTime = cl->lastGotoTime + gotoWaitTime;
		if (nextGotoTime > svs.time) {
			SV_SendServerCommand(cl, "print \"^7You ^1must wait ^5%i ^7seconds before using goto again.\n\"",
					((nextGotoTime - svs.time) / 1000) + 1);
			return;
		}
	}
	targetCl = SV_UserGetPlayerByHandle(Cmd_Argv(1));
	if (targetCl == NULL) {
		SV_SendServerCommand(cl, "print \"^7You specified an ^1invalid goto target ^7client.\n\"");
		return;
	}
	if (targetCl == cl) {
		SV_SendServerCommand(cl, "print \"^7You ^1can not ^7goto yourself! LoL o_O\n\"");
		return;
	}
	if (!targetCl->allowGoto) {
		SV_SendServerCommand(cl, "print \"^3%s ^1does not ^7allow goto\n\"", targetCl->name);
		return;
	}
	targetClId = targetCl - svs.clients;
	if (TEAM_SPECTATOR == atoi(Info_ValueForKey(sv.configstrings[548 + targetClId], "t"))) {
		SV_SendServerCommand(cl, "print \"^3%s ^7is currently ^1in spectators^7.\n\"", targetCl->name);
		return;
	}
	targetState = SV_GameClientNum(targetCl - svs.clients);
	if (targetState->pm_type != PM_NORMAL) {
		SV_SendServerCommand(cl, "print \"^3%s ^1isn't currently alive or isn't in-game.\n\"", targetCl->name);
		return;
	}
	/*
	*/
	VectorCopy(targetState->origin, myState->origin);
	if (targetState->pm_flags & PMF_DUCKED) {
		myState->pm_flags |= PMF_DUCKED;
	}
	cl->lastGotoTime = svs.time;
	SV_SendServerCommand(cl, "print \"^7You ^6goted ^7to ^3%s\n\"", targetCl->name);
	SV_SendServerCommand(targetCl, "print \"^3%s ^6goted ^7to you\n\"", cl->name);
	VectorClear(myState->velocity);
}

typedef struct {
	char	*name;
	void	(*func)( client_t *cl );
} ucmd_t;

static ucmd_t ucmds[] = {
	{"userinfo", SV_UpdateUserinfo_f},
	{"disconnect", SV_Disconnect_f},
	{"cp", SV_VerifyPaks_f},
	{"vdr", SV_ResetPureClient_f},
	{"download", SV_BeginDownload_f},
	{"nextdl", SV_NextDownload_f},
	{"stopdl", SV_StopDownload_f},
	{"donedl", SV_DoneDownload_f},
    {"modlogin", SV_ModLogin_f},
    {"modlogout", SV_ModLogout_f},
    {"mod", SV_ModCommand_f},

#ifdef USE_VOIP
	{"voip", SV_Voip_f},
#endif

	{NULL, NULL}
};

static ucmd_t ucmds_floodControl[] = {
	{"help", SV_DisplayGotoHelp_f},
	{"save", SV_SavePosition_f},
	{"load", SV_LoadPosition_f},
    {"saveposition", SV_SavePosition_f},
	{"loadposition", SV_LoadPosition_f},
    {"savelocation", SV_SavePosition_f},
	{"loadlocation", SV_LoadPosition_f},
    {"s", SV_SavePosition_f},
	{"l", SV_LoadPosition_f},
	{"allowgoto", SV_UserAllowGoto_f},
	{"goto", SV_Goto_f},
	{NULL, NULL}
};

/*
==================
SV_ExecuteClientCommand

Also called by bot code
==================
*/

// The value below is how many extra characters we reserve for every instance of '$' in a
// ut_radio, say, or similar client command.  Some jump maps have very long $location's.
// On these maps, it may be possible to crash the server if a carefully-crafted
// client command is sent.  The constant below may require further tweaking.  For example,
// a text of "$location" would have a total computed length of 25, because "$location" has
// 9 characters, and we increment that by 16 for the '$'.
#define STRLEN_INCREMENT_PER_DOLLAR_VAR 16

// Don't allow more than this many dollared-strings (e.g. $location) in a client command
// such as ut_radio and say.  Keep this value low for safety, in case some things like
// $location expand to very large strings in some maps.  There is really no reason to have
// more than 6 dollar vars (such as $weapon or $location) in things you tell other people.
#define MAX_DOLLAR_VARS 6

// When a radio text (as in "ut_radio 1 1 text") is sent, weird things start to happen
// when the text gets to be greater than 118 in length.  When the text is really large the
// server will crash.  There is an in-between gray zone above 118, but I don't really want
// to go there.  This is the maximum length of radio text that can be sent, taking into
// account increments due to presence of '$'.
#define MAX_RADIO_STRLEN 118

// Don't allow more than this text length in a command such as say.  I pulled this
// value out of my ass because I don't really know exactly when problems start to happen.
// This value takes into account increments due to the presence of '$'.
#define MAX_SAY_STRLEN 256

void SV_ExecuteClientCommand( client_t *cl, const char *s, qboolean clientOK ) {
	ucmd_t	*u;
	qboolean bProcessed = qfalse;
	int	argsFromOneMaxlen;
	int	charCount;
	int	dollarCount;
	int	i;
	char	*arg;
	qboolean exploitDetected;
	
	Cmd_TokenizeString( s );

	// see if it is a server level command
	for (u=ucmds ; u->name ; u++) {
		if (!strcmp (Cmd_Argv(0), u->name) ) {
			u->func( cl );
			bProcessed = qtrue;
			break;
		}
	}

	if (!bProcessed) {
		for (u = ucmds_floodControl; u->name; u++) {
			if (!Q_stricmp(Cmd_Argv(0), u->name)) {
				if (clientOK) { u->func(cl); }
				bProcessed = qtrue;
				break;
			}
		}
	}

	if (clientOK) {
		// pass unknown strings to the game
		if (!u->name && sv.state == SS_GAME) {
			Cmd_Args_Sanitize();

			if (cl->muted && (!Q_stricmp("say", Cmd_Argv(0)) ||
						!Q_stricmp("say_team", Cmd_Argv(0)) ||
						!Q_stricmp("tell", Cmd_Argv(0)) ||
						!Q_stricmp("ut_radio", Cmd_Argv(0)) ||
						!Q_stricmp("callvote", Cmd_Argv(0)))) {
				SV_SendServerCommand(cl, "print \"You are currently muted and may not perform that action.\n\"");
				return;
			}
			///////////////////////////////////////////////////////////////
			// separator for mutefix.patch and callvoteconnectprotect.patch
			///////////////////////////////////////////////////////////////
			if (!Q_stricmp("callvote", Cmd_Argv(0))) {
				int callvoteRequiredConnectTime = sv_callvoteRequiredConnectTime->integer;
				if (callvoteRequiredConnectTime > 0) {
					if (callvoteRequiredConnectTime > 1800) { // 30 minutes.
						callvoteRequiredConnectTime = 1800;
					}
					callvoteRequiredConnectTime *= 1000;
					if (svs.time - cl->lastConnectTime < callvoteRequiredConnectTime) {
						// Count the number of players in-game (not in spec).
						int playersInGame = 0;
						client_t *clTemp;
						for (i = 0, clTemp = svs.clients; i < sv_maxclients->integer; i++, clTemp++) {
							if (cl == clTemp || clTemp->state < CS_CONNECTED || clTemp->netchan.remoteAddress.type == NA_BOT) {
								continue;
							}
							if (TEAM_SPECTATOR == atoi(Info_ValueForKey(sv.configstrings[548 + (clTemp - svs.clients)], "t"))) {
								continue;
							}
							playersInGame++;
						}
						if (playersInGame > 0) {
							SV_SendServerCommand(cl, "print \"You recently connected and must wait another %i seconds "
									"before calling a vote.\n\"",
									((callvoteRequiredConnectTime - (svs.time - cl->lastConnectTime)) / 1000) + 1);
							return;
						}
					}
				}
			}
			/////////////////////////////////////////////////////////////////////
			// separator for callvoteconnectprotect.patch and cyclemaplimit.patch
			/////////////////////////////////////////////////////////////////////
			qboolean goingToCallvoteCyclemap = qfalse;
			if (Cmd_Argc() >= 2 && // If there are more arguments after "callvote cyclemap", it will still be a valid vote.
					!Q_stricmp("callvote", Cmd_Argv(0)) &&
					!Q_stricmp("cyclemap", Cmd_Argv(1))) {
				int callvoteCyclemapWaitTime = sv_callvoteCyclemapWaitTime->integer;
				if (sv.lastCallvoteCyclemapTime > 0 && // Allow cyclemap vote at start of map always.
						callvoteCyclemapWaitTime > 0) {
					if (callvoteCyclemapWaitTime > 1800) { // 30 minutes.
						callvoteCyclemapWaitTime = 1800;
					}
					callvoteCyclemapWaitTime *= 1000;
					int nextCallvoteCyclemapTime = sv.lastCallvoteCyclemapTime + callvoteCyclemapWaitTime;
					if (nextCallvoteCyclemapTime > svs.time) {
						SV_SendServerCommand(cl, "print \"Server won't accept repeated cyclemap vote for another %i seconds.\n\"",
								((nextCallvoteCyclemapTime - svs.time) / 1000) + 1);
						return;
					}
				}
				goingToCallvoteCyclemap = qtrue;
			}

			argsFromOneMaxlen = -1;
			if (Q_stricmp("say", Cmd_Argv(0)) == 0 ||
					Q_stricmp("say_team", Cmd_Argv(0)) == 0) {
				sv.lastSpecChat[0] = '\0';
				argsFromOneMaxlen = MAX_SAY_STRLEN;
			}
			else if (Q_stricmp("tell", Cmd_Argv(0)) == 0) {
				// A command will look like "tell 12 hi" or "tell foo hi".  The "12"
				// and "foo" in the examples will be counted towards MAX_SAY_STRLEN,
				// plus the space.
				argsFromOneMaxlen = MAX_SAY_STRLEN;
			}
			else if (Q_stricmp("ut_radio", Cmd_Argv(0)) == 0) {
				if (sv_disableRadio->integer > 0) {
					SV_SendServerCommand(cl, "print \"Radio is disabled on this server.\n\"");
					return;
				}
				// We add 4 to this value because in a command such as
				// "ut_radio 1 1 affirmative", the args at indices 1 and 2 each
				// have length 1 and there is a space after them.
				argsFromOneMaxlen = MAX_RADIO_STRLEN + 4;
			}
			if (argsFromOneMaxlen >= 0) {
				exploitDetected = qfalse;
				charCount = 0;
				dollarCount = 0;
				for (i = Cmd_Argc() - 1; i >= 1; i--) {
					arg = Cmd_Argv(i);
					while (*arg) {
						if (++charCount > argsFromOneMaxlen) {
							exploitDetected = qtrue; break;
						}
						if (*arg == '$') {
							if (++dollarCount > MAX_DOLLAR_VARS) {
								exploitDetected = qtrue; break;
							}
							charCount += STRLEN_INCREMENT_PER_DOLLAR_VAR;
							if (charCount > argsFromOneMaxlen) {
								exploitDetected = qtrue; break;
							}
						}
						arg++;
					}
					if (exploitDetected) { break; }
					if (i != 1) { // Cmd_ArgsFrom() will add space
						if (++charCount > argsFromOneMaxlen) {
							exploitDetected = qtrue; break;
						}
					}
				}
				if (exploitDetected) {
					Com_Printf("Buffer overflow exploit radio/say, possible attempt from %s\n",
						NET_AdrToString(cl->netchan.remoteAddress));
					SV_SendServerCommand(cl, "print \"Chat dropped due to message length constraints.\n\"");
					return;
				}
			}
			else if (Q_stricmp("callvote", Cmd_Argv(0)) == 0) {
				Com_Printf("Callvote from %s (client #%i, %s): %s\n",
						cl->name, (int) (cl - svs.clients),
						NET_AdrToString(cl->netchan.remoteAddress), Cmd_Args());
			}
			//////////////////////////////////////////////////////////
			// separator for logcallvote.patch and forceautojoin.patch
			//////////////////////////////////////////////////////////
			else if (Q_stricmp("team", Cmd_Argv(0)) == 0) {
				if (sv_forceAutojoin->integer > 0 && cl->netchan.remoteAddress.type != NA_BOT) {
					// The user interface buttons for joining red and blue in UrT send the strings
					// "team red" and "team blue" respectively.  The button for autojoin sends the string
					// "team free".  We're going to convert both "team red" and "team blue" to "team free".
					if (Q_stricmp("red", Cmd_Argv(1)) == 0 || Q_stricmp("blue", Cmd_Argv(1)) == 0) {
						Cmd_TokenizeString("team free");
						SV_SendServerCommand(cl, "print \"Forcing autojoin.\n\"");
					}
				}
				// Define a shorthand "team r" to mean "team red" and "team b" to mean "team blue".
				// This is done after the force autojoin logic above.  This will enable non-noobs who
				// know how to use the console and who know about this feature to bypass the force
				// autojoin feature.  This shorthand works regardless of the sv_forceAutojoin setting.
				if (Q_stricmp("r", Cmd_Argv(1)) == 0) {
					Cmd_TokenizeString("team red");
				}
				else if (Q_stricmp("b", Cmd_Argv(1)) == 0) {
					Cmd_TokenizeString("team blue");
				}
			}
			////////////////////////////////////////////////////////////
			// separator for forceautojoin.patch and cyclemaplimit.patch
			////////////////////////////////////////////////////////////
			if (goingToCallvoteCyclemap) { sv.inCallvoteCyclemap = qtrue; }
			VM_Call( gvm, GAME_CLIENT_COMMAND, cl - svs.clients );
			if (goingToCallvoteCyclemap) { sv.inCallvoteCyclemap = qfalse; }
		}
	}
	else if (!bProcessed)
		Com_DPrintf( "client text ignored for %s: %s\n", cl->name, Cmd_Argv(0) );
}

/*
===============
SV_ClientCommand
===============
*/
static qboolean SV_ClientCommand( client_t *cl, msg_t *msg ) {
	int		seq;
	const char	*s;
	qboolean clientOk = qtrue;

	seq = MSG_ReadLong( msg );
	s = MSG_ReadString( msg );

	// see if we have already executed it
	if ( cl->lastClientCommand >= seq ) {
		return qtrue;
	}

	Com_DPrintf( "clientCommand: %s : %i : %s\n", cl->name, seq, s );

	// drop the connection if we have somehow lost commands
	if ( seq > cl->lastClientCommand + 1 ) {
		Com_Printf( "Client %s lost %i clientCommands\n", cl->name, 
			seq - cl->lastClientCommand + 1 );
		SV_DropClient( cl, "Lost reliable commands" );
		return qfalse;
	}

	// malicious users may try using too many string commands
	// to lag other players.  If we decide that we want to stall
	// the command, we will stop processing the rest of the packet,
	// including the usercmd.  This causes flooders to lag themselves
	// but not other people
	// We don't do this when the client hasn't been active yet since its
	// normal to spam a lot of commands when downloading
	if ( !com_cl_running->integer && 
		cl->state >= CS_ACTIVE &&
		sv_floodProtect->integer ) {

		if (svs.time - cl->lastReliableTime < 1500) {
			// Allow two client commands every 1.5 seconds or so.
			if ((cl->lastReliableTime & 0x01) == 0) { // Lowest bit isn't set yet.
				cl->lastReliableTime |= 0x01; // Set lowest bit.
			}
			else { // Lowest bit is already set.
				// This is now at least our second client command in
				// a period of 1.5 seconds or so.  Ignore it.
				// TTimo - moved the ignored verbose to the actual processing in
				// SV_ExecuteClientCommand, only printing if the core doesn't intercept
				clientOk = qfalse;
			}
		}
		else {
			cl->lastReliableTime = (svs.time & (~0x01)); // Lowest bit 0.
		}
	}

	SV_ExecuteClientCommand( cl, s, clientOk );

	cl->lastClientCommand = seq;
	Com_sprintf(cl->lastClientCommandString, sizeof(cl->lastClientCommandString), "%s", s);

	return qtrue;		// continue procesing
}


//==================================================================================


/*
==================
SV_ClientThink

Also called by bot code
==================
*/
void SV_ClientThink (client_t *cl, usercmd_t *cmd) {
	cl->lastUsercmd = *cmd;

	if ( cl->state != CS_ACTIVE ) {
		return;		// may have been kicked during the last usercmd
	}

	VM_Call( gvm, GAME_CLIENT_THINK, cl - svs.clients );
}

/*
==================
SV_UserMove

The message usually contains all the movement commands 
that were in the last three packets, so that the information
in dropped packets can be recovered.

On very fast clients, there may be multiple usercmd packed into
each of the backup packets.
==================
*/
static void SV_UserMove( client_t *cl, msg_t *msg, qboolean delta ) {
	int			i, key;
	int			cmdCount;
	usercmd_t	nullcmd;
	usercmd_t	cmds[MAX_PACKET_USERCMDS];
	usercmd_t	*cmd, *oldcmd;

	if ( delta ) {
		cl->deltaMessage = cl->messageAcknowledge;
	} else {
		cl->deltaMessage = -1;
	}

	cmdCount = MSG_ReadByte( msg );

	if ( cmdCount < 1 ) {
		Com_Printf( "cmdCount < 1 for client %i\n", (int) (cl - svs.clients) );
		return;
	}

	if ( cmdCount > MAX_PACKET_USERCMDS ) {
		Com_Printf( "cmdCount > MAX_PACKET_USERCMDS for client %i\n", (int) (cl - svs.clients) );
		return;
	}

	// use the checksum feed in the key
	key = sv.checksumFeed;
	// also use the message acknowledge
	key ^= cl->messageAcknowledge;
	// also use the last acknowledged server command in the key
	key ^= Com_HashKey(cl->reliableCommands[ cl->reliableAcknowledge & (MAX_RELIABLE_COMMANDS-1) ], 32);

	Com_Memset( &nullcmd, 0, sizeof(nullcmd) );
	oldcmd = &nullcmd;
	for ( i = 0 ; i < cmdCount ; i++ ) {
		cmd = &cmds[i];
		MSG_ReadDeltaUsercmdKey( msg, key, oldcmd, cmd );
		oldcmd = cmd;
	}

	// save time for ping calculation
	cl->frames[ cl->messageAcknowledge & PACKET_MASK ].messageAcked = svs.time;

	// TTimo
	// catch the no-cp-yet situation before SV_ClientEnterWorld
	// if CS_ACTIVE, then it's time to trigger a new gamestate emission
	// if not, then we are getting remaining parasite usermove commands, which we should ignore
	if (sv_pure->integer != 0 && cl->pureAuthentic == 0 && !cl->gotCP) {
		if (cl->state == CS_ACTIVE)
		{
			// we didn't get a cp yet, don't assume anything and just send the gamestate all over again
			Com_DPrintf( "%s: didn't get cp command, resending gamestate\n", cl->name);
			SV_SendClientGameState( cl );
		}
		return;
	}			
	
	// if this is the first usercmd we have received
	// this gamestate, put the client into the world
	if ( cl->state == CS_PRIMED ) {
		SV_ClientEnterWorld( cl, &cmds[0] );
		// the moves can be processed normaly
	}
	
	// a bad cp command was sent, drop the client
	if (sv_pure->integer != 0 && cl->pureAuthentic == 0) {		
		SV_DropClient( cl, "Cannot validate pure client!");
		return;
	}

	if ( cl->state != CS_ACTIVE ) {
		cl->deltaMessage = -1;
		return;
	}

	// usually, the first couple commands will be duplicates
	// of ones we have previously received, but the servertimes
	// in the commands will cause them to be immediately discarded
	for ( i =  0 ; i < cmdCount ; i++ ) {
		// if this is a cmd from before a map_restart ignore it
		if ( cmds[i].serverTime > cmds[cmdCount-1].serverTime ) {
			continue;
		}
		// extremely lagged or cmd from before a map_restart
		//if ( cmds[i].serverTime > svs.time + 3000 ) {
		//	continue;
		//}
		// don't execute if this is an old cmd which is already executed
		// these old cmds are included when cl_packetdup > 0
		if ( cmds[i].serverTime <= cl->lastUsercmd.serverTime ) {
			continue;
		}
		SV_ClientThink (cl, &cmds[ i ]);
	}
}


#ifdef USE_VOIP
static
qboolean SV_ShouldIgnoreVoipSender(const client_t *cl)
{
	if (!sv_voip->integer)
		return qtrue;  // VoIP disabled on this server.
	else if (!cl->hasVoip)  // client doesn't have VoIP support?!
		return qtrue;
    
	// !!! FIXME: implement player blacklist.

	return qfalse;  // don't ignore.
}

static
void SV_UserVoip( client_t *cl, msg_t *msg ) {
	const int sender = (int) (cl - svs.clients);
	const int generation = MSG_ReadByte(msg);
	const int sequence = MSG_ReadLong(msg);
	const int frames = MSG_ReadByte(msg);
	const int recip1 = MSG_ReadLong(msg);
	const int recip2 = MSG_ReadLong(msg);
	const int recip3 = MSG_ReadLong(msg);
	const int packetsize = MSG_ReadShort(msg);
	byte encoded[sizeof (cl->voipPacket[0].data)];
	client_t *client = NULL;
	voipServerPacket_t *packet = NULL;
	int i;

	if (generation < 0)
		return;   // short/invalid packet, bail.
	else if (sequence < 0)
		return;   // short/invalid packet, bail.
	else if (frames < 0)
		return;   // short/invalid packet, bail.
	else if (recip1 < 0)
		return;   // short/invalid packet, bail.
	else if (recip2 < 0)
		return;   // short/invalid packet, bail.
	else if (recip3 < 0)
		return;   // short/invalid packet, bail.
	else if (packetsize < 0)
		return;   // short/invalid packet, bail.

	if (packetsize > sizeof (encoded)) {  // overlarge packet?
		int bytesleft = packetsize;
		while (bytesleft) {
			int br = bytesleft;
			if (br > sizeof (encoded))
				br = sizeof (encoded);
			MSG_ReadData(msg, encoded, br);
			bytesleft -= br;
		}
		return;   // overlarge packet, bail.
	}

	MSG_ReadData(msg, encoded, packetsize);

	if (SV_ShouldIgnoreVoipSender(cl))
		return;   // Blacklisted, disabled, etc.

	// !!! FIXME: see if we read past end of msg...

	// !!! FIXME: reject if not speex narrowband codec.
	// !!! FIXME: decide if this is bogus data?

	// (the three recip* values are 31 bits each (ignores sign bit so we can
	//  get a -1 error from MSG_ReadLong() ... ), allowing for 93 clients.)
	assert( sv_maxclients->integer < 93 );

	// decide who needs this VoIP packet sent to them...
	for (i = 0, client = svs.clients; i < sv_maxclients->integer ; i++, client++) {
		if (client->state != CS_ACTIVE)
			continue;  // not in the game yet, don't send to this guy.
		else if (i == sender)
			continue;  // don't send voice packet back to original author.
		else if (!client->hasVoip)
			continue;  // no VoIP support, or support disabled.
		else if (client->muteAllVoip)
			continue;  // client is ignoring everyone.
		else if (client->ignoreVoipFromClient[sender])
			continue;  // client is ignoring this talker.
		else if (*cl->downloadName)   // !!! FIXME: possible to DoS?
			continue;  // no VoIP allowed if downloading, to save bandwidth.
		else if ( ((i >= 0) && (i < 31)) && ((recip1 & (1 << (i-0))) == 0) )
			continue;  // not addressed to this player.
		else if ( ((i >= 31) && (i < 62)) && ((recip2 & (1 << (i-31))) == 0) )
			continue;  // not addressed to this player.
		else if ( ((i >= 62) && (i < 93)) && ((recip3 & (1 << (i-62))) == 0) )
			continue;  // not addressed to this player.

		// Transmit this packet to the client.
		// !!! FIXME: I don't like this queueing system.
		if (client->queuedVoipPackets >= (sizeof (client->voipPacket) / sizeof (client->voipPacket[0]))) {
			Com_Printf("Too many VoIP packets queued for client #%d\n", i);
			continue;  // no room for another packet right now.
		}

		packet = &client->voipPacket[client->queuedVoipPackets];
		packet->sender = sender;
		packet->frames = frames;
		packet->len = packetsize;
		packet->generation = generation;
		packet->sequence = sequence;
		memcpy(packet->data, encoded, packetsize);
		client->queuedVoipPackets++;
	}
}
#endif



/*
===========================================================================

USER CMD EXECUTION

===========================================================================
*/

/*
===================
SV_ExecuteClientMessage

Parse a client packet
===================
*/
void SV_ExecuteClientMessage( client_t *cl, msg_t *msg ) {
	int			c;
	int			serverId;

	MSG_Bitstream(msg);

	serverId = MSG_ReadLong( msg );
	cl->messageAcknowledge = MSG_ReadLong( msg );

	if (cl->messageAcknowledge < 0) {
		// usually only hackers create messages like this
		// it is more annoying for them to let them hanging
#ifndef NDEBUG
		SV_DropClient( cl, "DEBUG: illegible client message" );
#endif
		return;
	}

	cl->reliableAcknowledge = MSG_ReadLong( msg );

	// NOTE: when the client message is fux0red the acknowledgement numbers
	// can be out of range, this could cause the server to send thousands of server
	// commands which the server thinks are not yet acknowledged in SV_UpdateServerCommandsToClient
	if (cl->reliableAcknowledge < cl->reliableSequence - MAX_RELIABLE_COMMANDS) {
		// usually only hackers create messages like this
		// it is more annoying for them to let them hanging
#ifndef NDEBUG
		SV_DropClient( cl, "DEBUG: illegible client message" );
#endif
		cl->reliableAcknowledge = cl->reliableSequence;
		return;
	}
	// if this is a usercmd from a previous gamestate,
	// ignore it or retransmit the current gamestate
	// 
	// if the client was downloading, let it stay at whatever serverId and
	// gamestate it was at.  This allows it to keep downloading even when
	// the gamestate changes.  After the download is finished, we'll
	// notice and send it a new game state
	//
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=536
	// don't drop as long as previous command was a nextdl, after a dl is done, downloadName is set back to ""
	// but we still need to read the next message to move to next download or send gamestate
	// I don't like this hack though, it must have been working fine at some point, suspecting the fix is somewhere else
	if ( serverId != sv.serverId && !*cl->downloadName && !strstr(cl->lastClientCommandString, "nextdl") ) {
		if ( serverId >= sv.restartedServerId && serverId < sv.serverId ) { // TTimo - use a comparison here to catch multiple map_restart
			// they just haven't caught the map_restart yet
			Com_DPrintf("%s : ignoring pre map_restart / outdated client message\n", cl->name);
			return;
		}
		// if we can tell that the client has dropped the last
		// gamestate we sent them, resend it
		if ( cl->messageAcknowledge > cl->gamestateMessageNum ) {
			Com_DPrintf( "%s : dropped gamestate, resending\n", cl->name );
			SV_SendClientGameState( cl );
		}
		return;
	}

	// this client has acknowledged the new gamestate so it's
	// safe to start sending it the real time again
	if( cl->oldServerTime && serverId == sv.serverId ){
		Com_DPrintf( "%s acknowledged gamestate\n", cl->name );
		cl->oldServerTime = 0;
	}

	// read optional clientCommand strings
	do {
		c = MSG_ReadByte( msg );

		// See if this is an extension command after the EOF, which means we
		//  got data that a legacy server should ignore.
		if ((c == clc_EOF) && (MSG_LookaheadByte( msg ) == clc_extension)) {
			MSG_ReadByte( msg );  // throw the clc_extension byte away.
			c = MSG_ReadByte( msg );  // something legacy servers can't do!
			// sometimes you get a clc_extension at end of stream...dangling
			//  bits in the huffman decoder giving a bogus value?
			if (c == -1) {
				c = clc_EOF;
			}
		}

		if ( c == clc_EOF ) {
			break;
		}

		if ( c != clc_clientCommand ) {
			break;
		}
		if ( !SV_ClientCommand( cl, msg ) ) {
			return;	// we couldn't execute it because of the flood protection
		}
		if (cl->state == CS_ZOMBIE) {
			return;	// disconnect command
		}
	} while ( 1 );

	// read the usercmd_t
	if ( c == clc_move ) {
		SV_UserMove( cl, msg, qtrue );
	} else if ( c == clc_moveNoDelta ) {
		SV_UserMove( cl, msg, qfalse );
	} else if ( c == clc_voip ) {
#ifdef USE_VOIP
		SV_UserVoip( cl, msg );
#endif
	} else if ( c != clc_EOF ) {
		Com_Printf( "WARNING: bad command byte for client %i\n", (int) (cl - svs.clients) );
	}
//	if ( msg->readcount != msg->cursize ) {
//		Com_Printf( "WARNING: Junk at end of packet for client %i\n", cl - svs.clients );
//	}
}
