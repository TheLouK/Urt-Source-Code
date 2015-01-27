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

#include "server.h"

serverStatic_t	svs;				// persistant server info
server_t		sv;					// local server
vm_t			*gvm = NULL;		// game virtual machine

cvar_t	*sv_fps;					// time rate for running non-clients
cvar_t	*sv_timeout;				// seconds without any message
cvar_t	*sv_zombietime;				// seconds to sink messages after disconnect
cvar_t	*sv_rconPassword;			// password for remote server commands
cvar_t	*sv_rconRecoveryPassword;	// password for recovery the password remote server commands
cvar_t	*sv_rconAllowedSpamIP;			// this ip is allowed to do spam on rcon
cvar_t	*sv_privatePassword;		// password for the privateClient slots
cvar_t	*sv_allowDownload;
cvar_t	*sv_maxclients;

cvar_t	*sv_privateClients;			// number of clients reserved for password
cvar_t	*sv_hostname;
cvar_t	*sv_master[MAX_MASTER_SERVERS];		// master server ip address
cvar_t	*sv_reconnectlimit;			// minimum seconds between connect messages
cvar_t	*sv_showloss;				// report when usercmds are lost
cvar_t	*sv_padPackets;				// add nop bytes to messages
cvar_t	*sv_killserver;				// menu system can set to 1 to shut server down
cvar_t	*sv_mapname;
cvar_t	*sv_mapChecksum;
cvar_t	*sv_serverid;
cvar_t	*sv_minRate;
cvar_t	*sv_maxRate;
cvar_t	*sv_minPing;
cvar_t	*sv_maxPing;
cvar_t	*sv_gametype;
cvar_t	*sv_pure;
cvar_t	*sv_newpurelist;
cvar_t	*sv_floodProtect;
cvar_t	*sv_lanForceRate;			// dedicated 1 (LAN) server forces local client rates to 99999 (bug #491)
cvar_t	*sv_strictAuth;
cvar_t	*sv_clientsPerIp;

cvar_t	*sv_demonotice;				// notice to print to a client being recorded server-side
cvar_t  *sv_tellprefix;
cvar_t  *sv_sayprefix;
cvar_t 	*sv_demofolder;				//@Barbatos - the name of the folder that contains server-side demos

cvar_t  *sv_hideCmd;
cvar_t  *sv_hideCmdList;
cvar_t  *sv_noFallDamage;
cvar_t  *sv_mod;
cvar_t  *sv_free_stamina;
cvar_t  *sv_free_walljumps;
cvar_t  *sv_free_colorScore;
cvar_t  *sv_free_weapons;
cvar_t  *sv_red_stamina;
cvar_t  *sv_red_walljumps;
cvar_t  *sv_red_colorScore;
cvar_t  *sv_red_weapons;
cvar_t  *sv_blue_stamina;
cvar_t  *sv_blue_walljumps;
cvar_t  *sv_blue_colorScore;
cvar_t  *sv_blue_weapons;
cvar_t  *sv_disableRadio;
cvar_t  *sv_coloredNames;
cvar_t  *sv_disableServerCommand;
cvar_t	*sv_forceAutojoin;
cvar_t	*sv_totalMute;
cvar_t	*sv_callvoteRequiredConnectTime;
cvar_t	*sv_logRconArgs;

cvar_t	*sv_test;

// weapons
cvar_t  *sv_knife_clips;
cvar_t  *sv_knife_bullets;
cvar_t  *sv_knife_slash_firetime;
cvar_t  *sv_knife_throw_firetime;
cvar_t  *sv_beretta_clips;
cvar_t  *sv_beretta_bullets;
cvar_t  *sv_beretta_firetime;
cvar_t  *sv_de_clips;
cvar_t  *sv_de_bullets;
cvar_t  *sv_spas_clips;
cvar_t  *sv_spas_bullets;
cvar_t  *sv_mp5_clips;
cvar_t  *sv_mp5_bullets;
cvar_t  *sv_ump_clips;
cvar_t  *sv_ump_bullets;
cvar_t  *sv_hk69_clips;
cvar_t  *sv_hk69_bullets;
cvar_t  *sv_lr300_clips;
cvar_t  *sv_lr300_bullets;
cvar_t  *sv_g36_clips;
cvar_t  *sv_g36_bullets;
cvar_t  *sv_psg_clips;
cvar_t  *sv_psg_bullets;
cvar_t  *sv_he_clips;
cvar_t  *sv_he_bullets;
cvar_t  *sv_flash_clips;
cvar_t  *sv_flash_bullets;
cvar_t  *sv_smoke_clips;
cvar_t  *sv_smoke_bullets;
cvar_t  *sv_sr8_clips;
cvar_t  *sv_sr8_bullets;
cvar_t  *sv_ak_clips;
cvar_t  *sv_ak_bullets;
cvar_t  *sv_bomb_clips;
cvar_t  *sv_bomb_bullets;
cvar_t  *sv_negev_clips;
cvar_t  *sv_negev_bullets;
cvar_t  *sv_m4_clips;
cvar_t  *sv_m4_bullets;
cvar_t  *sv_glock_clips;
cvar_t  *sv_glock_bullets;
cvar_t  *sv_colt_clips;
cvar_t  *sv_colt_bullets;
cvar_t  *sv_mac11_clips;
cvar_t  *sv_mac11_bullets;
//@Barbatos
#ifdef USE_AUTH
cvar_t	*sv_authServerIP;
cvar_t  *sv_auth_engine;
#endif

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/

/*
===============
SV_ExpandNewlines

Converts newlines to "\n" so a line prints nicer
===============
*/
char	*SV_ExpandNewlines( char *in ) {
	static	char	string[1024];
	int		l;

	l = 0;
	while ( *in && l < sizeof(string) - 3 ) {
		if ( *in == '\n' ) {
			string[l++] = '\\';
			string[l++] = 'n';
		} else {
			string[l++] = *in;
		}
		in++;
	}
	string[l] = 0;

	return string;
}

/*
======================
SV_ReplacePendingServerCommands

  This is ugly
======================
*/
int SV_ReplacePendingServerCommands( client_t *client, const char *cmd ) {
	int i, index, csnum1, csnum2;

	for ( i = client->reliableSent+1; i <= client->reliableSequence; i++ ) {
		index = i & ( MAX_RELIABLE_COMMANDS - 1 );
		//
		if ( !Q_strncmp(cmd, client->reliableCommands[ index ], strlen("cs")) ) {
			sscanf(cmd, "cs %i", &csnum1);
			sscanf(client->reliableCommands[ index ], "cs %i", &csnum2);
			if ( csnum1 == csnum2 ) {
				Q_strncpyz( client->reliableCommands[ index ], cmd, sizeof( client->reliableCommands[ index ] ) );
				/*
				if ( client->netchan.remoteAddress.type != NA_BOT ) {
					Com_Printf( "WARNING: client %i removed double pending config string %i: %s\n", client-svs.clients, csnum1, cmd );
				}
				*/
				return qtrue;
			}
		}
	}
	return qfalse;
}

/*
======================
SV_AddServerCommand

The given command will be transmitted to the client, and is guaranteed to
not have future snapshot_t executed before it is executed
======================
*/
void SV_AddServerCommand( client_t *client, const char *cmd ) {
	int		index, i;

	// this is very ugly but it's also a waste to for instance send multiple config string updates
	// for the same config string index in one snapshot
//	if ( SV_ReplacePendingServerCommands( client, cmd ) ) {
//		return;
//	}

	// do not send commands until the gamestate has been sent
	if( client->state < CS_PRIMED )
		return;

	client->reliableSequence++;
	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	// we check == instead of >= so a broadcast print added by SV_DropClient()
	// doesn't cause a recursive drop client
	if ( client->reliableSequence - client->reliableAcknowledge == MAX_RELIABLE_COMMANDS + 1 ) {
		Com_Printf( "===== pending server commands =====\n" );
		for ( i = client->reliableAcknowledge + 1 ; i <= client->reliableSequence ; i++ ) {
			Com_Printf( "cmd %5d: %s\n", i, client->reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
		}
		Com_Printf( "cmd %5d: %s\n", i, cmd );
		SV_DropClient( client, "Server command overflow" );
		return;
	}
	index = client->reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( client->reliableCommands[ index ], cmd, sizeof( client->reliableCommands[ index ] ) );
}

//check a string
int str_CheckString(char sub[], char s[]) {
    int i, j;
    for (i=0; s[i]; i++) {
        for (j=0; sub[j] && tolower(sub[j]) == tolower(s[i+j]); j++);
        if (!sub[j]) {
            return i;
        }
    }
    return -1;
}

//check if a string match some strings
int str_MatchCmd( char *s ) {
    int     i,j,n=0;
    char    *cmdList = sv_hideCmdList->string;
    char    *cmd[20];    
    char    *temp=strdup(cmdList);
    char    *separator = " "; // use a space as separator
    
    // explode sv_hidecmdlist
    cmd[n]=strtok(temp, separator);
    while(cmd[n] && n<4) cmd[++n]=strtok(NULL, separator);
    
    if ((i = str_CheckString("chat",s)) != -1){
        for(j=0; j<n; j++) {
            if ((i = str_CheckString(cmd[j],s)) != -1){
                return 1;
            }
        }
        free(temp);
    }    
    
    return 0;
}

/*
=================
SV_SendServerCommand

Sends a reliable command string to be interpreted by
the client game module: "cp", "print", "chat", etc
A NULL client will broadcast to all clients

Vega: questa funzione viene chiamata passando come *cl (destinatario del messaggio) tutti i client iterativamente.
    In questo modo il messaggio viene mandato a tutti. Di questo se ne occupa il metodo SV_AddServerCommand ( cl, message),
    dove cl è il destinatario.
=================
*/
void QDECL SV_SendServerCommand(client_t *cl, const char *fmt, ...) {
	va_list		argptr;
	byte		message[MAX_MSGLEN];
	client_t	*client;
	int			j;
	int			msglen;
	
	va_start (argptr,fmt);
	Q_vsnprintf ((char *)message, sizeof(message), fmt,argptr);
	va_end (argptr);

	msglen = strlen((char *)message);
	
	// Fix to http://aluigi.altervista.org/adv/q3msgboom-adv.txt
	// The actual cause of the bug is probably further downstream
	// and should maybe be addressed later, but this certainly
	// fixes the problem for now
	if ( msglen > 1022 ) {
		return;
	}
    
    if (cl != NULL) {
        if (!strcmp((char *) message, "print \"The admin muted you: you cannot talk\"\n")) {
            cl->muted = qtrue;
        }
        else if (!strcmp((char *) message, "print \"The admin unmuted you\"\n")) {
            cl->muted = qfalse;
        }
        else if (!strcmp((char *) message, "print \"You have been unmuted\"\n")) {
            cl->muted = qfalse;
        }
    }
	if (!strcmp((char *) message, "print \"godmode ON\n\"")) {
		Cmd_ExecuteString (va("bigtext \"%s ^7is now a ^2GoD\"", cl->cname));
	}
	if (!strcmp((char *) message, "print \"godmode OFF\n\"")) {
		Cmd_ExecuteString (va("bigtext \"%s ^7is ^1no more^7 a ^2GoD\"", cl->cname));
	}
    
	Com_Printf("%s\n",(char *) message);
	
	if (sv.incognitoJoinSpec &&
		cl == NULL &&
		(!Q_strncmp((char *) message, "print \"", 7)) &&
		msglen >= 27 + 7 &&
		!strcmp("^7 joined the spectators.\n\"", ((char *) message) + msglen - 27)) {
		return;
	}
    
    // hide message
	int hideThisMessage = 0;
	if (sv_hideCmd->integer > 0) {
		if(str_MatchCmd( (char*)message )== 1){
			hideThisMessage = 1;
		}
	}
	if (hideThisMessage==1) {
        // cl sono tutti i client connessi, sendservercommand viene mandato numclients volte        
        SV_SendServerCommand(cl, "chat \"^3~^7> %s\"",message);
		return;
	}// end hide message
    
    if (sv_disableServerCommand->integer > 0){
        // if chat colored string it's already printed with SV_SendMyCommand (if coloredStrings>0) return
        return;
    }
    if ( cl != NULL ) {        
        SV_AddServerCommand( cl, (char *)message );// cl destinatario mex
        return;
    }
    
    // hack to echo broadcast prints to console
    if ( com_dedicated->integer && !strncmp( (char *)message, "print", 5) ) {
        Com_Printf ("broadcast: %s\n", SV_ExpandNewlines((char *)message) );
    }

    // send the data to all relevent clients
    for (j = 0, client = svs.clients; j < sv_maxclients->integer ; j++, client++) {
        SV_AddServerCommand( client, (char *)message );
    }
    
}

void QDECL SV_SendMyCommand(client_t *cl, const char *fmt, ...) {
    va_list		argptr;
    byte		message[MAX_MSGLEN];
    client_t	*client;
    int			j;
    
    va_start (argptr,fmt);
    Q_vsnprintf ((char *)message, sizeof(message), fmt,argptr);
    va_end (argptr);
    
    // Fix to http://aluigi.altervista.org/adv/q3msgboom-adv.txt
    // The actual cause of the bug is probably further downstream
    // and should maybe be addressed later, but this certainly
    // fixes the problem for now
    if ( strlen ((char *)message) > 1022 ) {
        return;
    }
    
    // hide message
    int hideThisMessage = 0;
    if (sv_hideCmd->integer > 0) {
        if(str_MatchCmd( (char*)message )== 1){
            hideThisMessage = 1;
        }
    }
    if (hideThisMessage==1) {
        // cl sono tutti i client connessi, sendservercommand viene mandato numclients volte        
        //SV_SendServerCommand(cl, "chat \"^3~^7> %s\"",message);
        return;
    }// end hide message
    
    if ( cl != NULL ) {        
        SV_AddServerCommand( cl, (char *)message );// cl destinatario mex
        return;
    }
    
    // hack to echo broadcast prints to console
    if ( com_dedicated->integer && !strncmp( (char *)message, "print", 5) ) {
        Com_Printf ("broadcast: %s\n", SV_ExpandNewlines((char *)message) );
    }
    
    // send the data to all relevent clients
    for (j = 0, client = svs.clients; j < sv_maxclients->integer ; j++, client++) {
        SV_AddServerCommand( client, (char *)message );
    }
}


/*
==============================================================================

MASTER SERVER FUNCTIONS

==============================================================================
*/

/*
================
SV_MasterHeartbeat

Send a message to the masters every few minutes to
let it know we are alive, and log information.
We will also have a heartbeat sent when a server
changes from empty to non-empty, and full to non-full,
but not on every player enter or exit.
================
*/
#define	HEARTBEAT_MSEC	300*1000
#define	HEARTBEAT_GAME	"QuakeArena-1"
void SV_MasterHeartbeat( void ) {
	static netadr_t	adr[MAX_MASTER_SERVERS];
	int			i;

	// "dedicated 1" is for lan play, "dedicated 2" is for inet public play
	if ( !com_dedicated || com_dedicated->integer != 2 ) {
		return;		// only dedicated servers send heartbeats
	}

	// if not time yet, don't send anything
	if ( svs.time < svs.nextHeartbeatTime ) {
		return;
	}
	svs.nextHeartbeatTime = svs.time + HEARTBEAT_MSEC;


	#ifdef USE_AUTH
	VM_Call( gvm, GAME_AUTHSERVER_HEARTBEAT );
	#endif
	
	// send to group masters
	for ( i = 0 ; i < MAX_MASTER_SERVERS ; i++ ) {
		if ( !sv_master[i]->string[0] ) {
			continue;
		}

		// see if we haven't already resolved the name
		// resolving usually causes hitches on win95, so only
		// do it when needed
		if ( sv_master[i]->modified ) {
			sv_master[i]->modified = qfalse;
	
			Com_Printf( "Resolving %s\n", sv_master[i]->string );
			if ( !NET_StringToAdr( sv_master[i]->string, &adr[i] ) ) {
				// if the address failed to resolve, clear it
				// so we don't take repeated dns hits
				Com_Printf( "Couldn't resolve address: %s\n", sv_master[i]->string );
				Cvar_Set( sv_master[i]->name, "" );
				sv_master[i]->modified = qfalse;
				continue;
			}
			if ( !strchr( sv_master[i]->string, ':' ) ) {
				adr[i].port = BigShort( PORT_MASTER );
			}
			Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", sv_master[i]->string,
				adr[i].ip[0], adr[i].ip[1], adr[i].ip[2], adr[i].ip[3],
				BigShort( adr[i].port ) );
		}


		Com_Printf ("Sending heartbeat to %s\n", sv_master[i]->string );
		// this command should be changed if the server info / status format
		// ever incompatably changes
		NET_OutOfBandPrint( NS_SERVER, adr[i], "heartbeat %s\n", HEARTBEAT_GAME );
	}
}

/*
=================
SV_MasterShutdown

Informs all masters that this server is going down
=================
*/
void SV_MasterShutdown( void ) {
	// send a hearbeat right now
	svs.nextHeartbeatTime = -9999;
	SV_MasterHeartbeat();

	// send it again to minimize chance of drops
	svs.nextHeartbeatTime = -9999;
	SV_MasterHeartbeat();

	// when the master tries to poll the server, it won't respond, so
	// it will be removed from the list
	
	#ifdef USE_AUTH
	VM_Call( gvm, GAME_AUTHSERVER_SHUTDOWN );
	#endif
}


/*
==============================================================================

CONNECTIONLESS COMMANDS

==============================================================================
*/

/*
================
SVC_Status

Responds with all the info that qplug or qspy can see about the server
and all connected players.  Used for getting detailed information after
the simple info query.
================
*/
void SVC_Status( netadr_t from ) {
	char	player[1024];
	char	status[MAX_MSGLEN];
	int		i;
	client_t	*cl;
	playerState_t	*ps;
	int		statusLength;
	int		playerLength;
	char	infostring[MAX_INFO_STRING];

	// ignore if we are in single player
	if ( Cvar_VariableValue( "g_gametype" ) == GT_SINGLE_PLAYER ) {
		return;
	}

	strcpy( infostring, Cvar_InfoString( CVAR_SERVERINFO ) );

	// echo back the parameter to status. so master servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv(1) );

	status[0] = 0;
	statusLength = 0;

	for (i=0 ; i < sv_maxclients->integer ; i++) {
		cl = &svs.clients[i];
		if ( cl->state >= CS_CONNECTED ) {
			ps = SV_GameClientNum( i );
			Com_sprintf (player, sizeof(player), "%i %i \"%s\"\n",
				ps->persistant[PERS_SCORE], cl->ping, cl->name);
			playerLength = strlen(player);
			if (statusLength + playerLength >= sizeof(status) ) {
				break;		// can't hold any more
			}
			strcpy (status + statusLength, player);
			statusLength += playerLength;
		}
	}

	NET_OutOfBandPrint( NS_SERVER, from, "statusResponse\n%s\n%s", infostring, status );
}

/*
================
SVC_Info

Responds with a short info message that should be enough to determine
if a user is interested in a server to do a full status
================
*/
void SVC_Info( netadr_t from ) {
	int		i, count, bots;
	char	*gamedir;
	char	infostring[MAX_INFO_STRING];

	// ignore if we are in single player
	if ( Cvar_VariableValue( "g_gametype" ) == GT_SINGLE_PLAYER || Cvar_VariableValue("ui_singlePlayerActive")) {
		return;
	}

	/*
	 * Check whether Cmd_Argv(1) has a sane length. This was not done in the original Quake3 version which led
	 * to the Infostring bug discovered by Luigi Auriemma. See http://aluigi.altervista.org/ for the advisory.
	 */

	// A maximum challenge length of 128 should be more than plenty.
	if(strlen(Cmd_Argv(1)) > 128)
		return;

	// don't count privateclients
	count = 0;
	bots = 0;
	for ( i = sv_privateClients->integer ; i < sv_maxclients->integer ; i++ ) {
		if ( svs.clients[i].state >= CS_CONNECTED ) {
			count++;

			if (svs.clients[i].netchan.remoteAddress.type == NA_BOT)
				bots++;
		}
	}

	infostring[0] = 0;

	// echo back the parameter to status. so servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv(1) );

	Info_SetValueForKey( infostring, "protocol", va("%i", PROTOCOL_VERSION) );
	Info_SetValueForKey( infostring, "hostname", sv_hostname->string );
	Info_SetValueForKey( infostring, "mapname", sv_mapname->string );
	Info_SetValueForKey( infostring, "clients", va("%i", count) );
	Info_SetValueForKey( infostring, "bots", va("%i", bots) );
	Info_SetValueForKey( infostring, "sv_maxclients",
		va("%i", sv_maxclients->integer - sv_privateClients->integer ) );
	Info_SetValueForKey( infostring, "gametype", va("%i", sv_gametype->integer ) );
	Info_SetValueForKey( infostring, "pure", va("%i", sv_pure->integer ) );
	
	//@Barbatos
	#ifdef USE_AUTH
	Info_SetValueForKey( infostring, "auth", Cvar_VariableString("auth") );
	#endif

	//@Barbatos: if it's a passworded server, let the client know (for the server browser)
	if(Cvar_VariableValue("g_needpass") == 1)
		Info_SetValueForKey( infostring, "password", va("%i", 1));
		
	if( sv_minPing->integer ) {
		Info_SetValueForKey( infostring, "minPing", va("%i", sv_minPing->integer) );
	}
	if( sv_maxPing->integer ) {
		Info_SetValueForKey( infostring, "maxPing", va("%i", sv_maxPing->integer) );
	}
	gamedir = Cvar_VariableString( "fs_game" );
	if( *gamedir ) {
		Info_SetValueForKey( infostring, "game", gamedir );
	}

	Info_SetValueForKey(infostring, "modversion", Cvar_VariableString("g_modversion"));

	NET_OutOfBandPrint( NS_SERVER, from, "infoResponse\n%s", infostring );
}

/*
================
SVC_FlushRedirect

================
*/
void SV_FlushRedirect( char *outputbuf ) {
	NET_OutOfBandPrint( NS_SERVER, svs.redirectAddress, "print\n%s", outputbuf );
}

/*
===============
SVC_RconRecoveryRemoteCommand

An rcon packet arrived from the network.
Shift down the remaining args
Redirect all printfs
===============
*/
void SVC_RconRecoveryRemoteCommand( netadr_t from, msg_t *msg ) {
	qboolean	valid;
	unsigned int time;
	// TTimo - scaled down to accumulate, but not overflow anything network wise, print wise etc.
	// (OOB messages are the bottleneck here)
#define SV_OUTPUTBUF_LENGTH (1024 - 16)
	char		sv_outputbuf[SV_OUTPUTBUF_LENGTH];
	static unsigned int lasttime = 0;

	// TTimo - https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=534
	time = Com_Milliseconds();
	
	if ( !strlen( sv_rconRecoveryPassword->string ) || strcmp (Cmd_Argv(1), sv_rconRecoveryPassword->string) )
	{
		// MaJ - If the rconpassword is bad and one just happned recently, don't spam the log file, just die.
		if ( (unsigned)( time - lasttime ) < 600u )
			return;
			
		valid = qfalse;
		Com_Printf ("Bad rcon recovery from %s:\n%s\n", NET_AdrToString (from), Cmd_Argv(2) );
	} else {
		// MaJ - If the rconpassword is good, allow it much sooner than a bad one.
		if ( (unsigned)( time - lasttime ) < 180u )
			return;

		
		valid = qtrue;
		Com_Printf ("Rcon recovery from %s:\n%s\n", NET_AdrToString (from), Cmd_Argv(2) );
	}
	lasttime = time;

	// start redirecting all print outputs to the packet
	svs.redirectAddress = from;
	Com_BeginRedirect (sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect);

	if ( !strlen( sv_rconPassword->string ) ) {
		Com_Printf ("No rcon recovery password set on the server.\n");
	} else if ( !valid ) {
		Com_Printf ("Bad rcon recovery password.\n");
	} else {
		Com_Printf ("rconPassword %s\n" , sv_rconPassword->string );
	}

	Com_EndRedirect ();
}

/*
===============
SVC_RemoteCommand

An rcon packet arrived from the network.
Shift down the remaining args
Redirect all printfs
===============
*/
void SVC_RemoteCommand( netadr_t from, msg_t *msg ) {
	qboolean	valid;
	unsigned int time;
	char		remaining[1024];
	netadr_t	allowedSpamIPAdress;
	// TTimo - scaled down to accumulate, but not overflow anything network wise, print wise etc.
	// (OOB messages are the bottleneck here)
#define SV_OUTPUTBUF_LENGTH (1024 - 16)
	char		sv_outputbuf[SV_OUTPUTBUF_LENGTH];
	static unsigned int lasttime = 0;
	char *cmd_aux;

	// TTimo - https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=534
	time = Com_Milliseconds();
	
	
	NET_StringToAdr( sv_rconAllowedSpamIP->string , &allowedSpamIPAdress);
	
	
	if ( !strlen( sv_rconPassword->string ) || strcmp (Cmd_Argv(1), sv_rconPassword->string) )
	{
		// let's the sv_rconAllowedSpamIP do spam rcon
		if ( ( !strlen( sv_rconAllowedSpamIP->string ) || !NET_CompareBaseAdr( from , allowedSpamIPAdress ) ) && !NET_IsLocalAddress(from) ){
			// MaJ - If the rconpassword is bad and one just happned recently, don't spam the log file, just die.
			if ( (unsigned)( time - lasttime ) < 600u )
				return;
		}
		
		valid = qfalse;
		
		if (sv_logRconArgs->integer > 0) {
			Com_Printf("Bad rcon from %s\n", NET_AdrToString(from));
		}
		else {
			Com_Printf("Bad rcon from %s:\n%s\n", NET_AdrToString(from), Cmd_Argv(2));
		}
	} else {
	
		// let's the sv_rconAllowedSpamIP do spam rcon
		if ( ( !strlen( sv_rconAllowedSpamIP->string ) || !NET_CompareBaseAdr( from , allowedSpamIPAdress ) ) && !NET_IsLocalAddress(from) ){
			// MaJ - If the rconpassword is good, allow it much sooner than a bad one.
			if ( (unsigned)( time - lasttime ) < 180u )
				return;
		}
		
		valid = qtrue;
		/*Com_Printf ("Rcon from %s:\n%s\n", NET_AdrToString (from), Cmd_Argv(2) );
	}
	lasttime = time;

	// start redirecting all print outputs to the packet
	svs.redirectAddress = from;
	Com_BeginRedirect (sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect);

	if ( !strlen( sv_rconPassword->string ) ) {
		Com_Printf ("No rconpassword set on the server.\n");
	} else if ( !valid ) {
		Com_Printf ("Bad rconpassword.\n");
	} else {*/
		remaining[0] = 0;
		
		// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=543
		// get the command directly, "rcon <pass> <command>" to avoid quoting issues
		// extract the command by walking
		// since the cmd formatting can fuckup (amount of spaces), using a dumb step by step parsing
		cmd_aux = Cmd_Cmd();
		cmd_aux+=4;
		while(cmd_aux[0]==' ')
			cmd_aux++;
		while(cmd_aux[0] && cmd_aux[0]!=' ') // password
			cmd_aux++;
		while(cmd_aux[0]==' ')
			cmd_aux++;
		
		Q_strcat( remaining, sizeof(remaining), cmd_aux);
		
//		Cmd_ExecuteString (remaining);
		
		if (sv_logRconArgs->integer > 0) {
			Com_Printf("Rcon from %s: %s\n", NET_AdrToString(from), remaining);
		}
		else {
			Com_Printf("Rcon from %s:\n%s\n", NET_AdrToString(from), Cmd_Argv(2));
		}
	}
	lasttime = time;
	
	// start redirecting all print outputs to the packet
	svs.redirectAddress = from;
	Com_BeginRedirect (sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect);
	
	if ( !strlen( sv_rconPassword->string ) ) {
		Com_Printf ("No rconpassword set on the server.\n");
	} else if ( !valid ) {
		Com_Printf ("Bad rconpassword.\n");
	} else {		
		Cmd_ExecuteString (remaining);
	}
	Com_EndRedirect ();
}

/*
=================
SV_CheckDRDoS

DRDoS stands for "Distributed Reflected Denial of Service".
See here: http://www.lemuria.org/security/application-drdos.html

Returns qfalse if we're good.  qtrue return value means we need to block.
If the address isn't NA_IP, it's automatically denied.
=================
*/
qboolean SV_CheckDRDoS(netadr_t from)
{
	int		i;
	int		globalCount;
	int		specificCount;
	receipt_t	*receipt;
	netadr_t	exactFrom;
	int		oldest;
	int		oldestTime;
	static int	lastGlobalLogTime = 0;
	static int	lastSpecificLogTime = 0;

	// Usually the network is smart enough to not allow incoming UDP packets
	// with a source address being a spoofed LAN address.  Even if that's not
	// the case, sending packets to other hosts in the LAN is not a big deal.
	// NA_LOOPBACK qualifies as a LAN address.
	if (Sys_IsLANAddress(from)) { return qfalse; }

	exactFrom = from;
	
	if (from.type == NA_IP) {
		from.ip[3] = 0; // xx.xx.xx.0
	}
	else {
		// So we got a connectionless packet but it's not IPv4, so
		// what is it?  I don't care, it doesn't matter, we'll just block it.
		// This probably won't even happen.
		return qtrue;
	}

	// Count receipts in last 2 seconds.
	globalCount = 0;
	specificCount = 0;
	receipt = &svs.infoReceipts[0];
	oldest = 0;
	oldestTime = 0x7fffffff;
	for (i = 0; i < MAX_INFO_RECEIPTS; i++, receipt++) {
		if (receipt->time + 2000 > svs.time) {
			if (receipt->time) {
				// When the server starts, all receipt times are at zero.  Furthermore,
				// svs.time is close to zero.  We check that the receipt time is already
				// set so that during the first two seconds after server starts, queries
				// from the master servers don't get ignored.  As a consequence a potentially
				// unlimited number of getinfo+getstatus responses may be sent during the
				// first frame of a server's life.
				globalCount++;
			}
			if (NET_CompareBaseAdr(from, receipt->adr)) {
				specificCount++;
			}
		}
		if (receipt->time < oldestTime) {
			oldestTime = receipt->time;
			oldest = i;
		}
	}

	if (globalCount == MAX_INFO_RECEIPTS) { // All receipts happened in last 2 seconds.
		if (lastGlobalLogTime + 1000 <= svs.time){ // Limit one log every second.
			Com_Printf("Detected flood of getinfo/getstatus connectionless packets\n");
			lastGlobalLogTime = svs.time;
		}
		return qtrue;
	}
	if (specificCount >= 3) { // Already sent 3 to this IP in last 2 seconds.
		if (lastSpecificLogTime + 1000 <= svs.time) { // Limit one log every second.
			Com_DPrintf("Possible DRDoS attack to address %i.%i.%i.%i, ignoring getinfo/getstatus connectionless packet\n",
					exactFrom.ip[0], exactFrom.ip[1], exactFrom.ip[2], exactFrom.ip[3]);
			lastSpecificLogTime = svs.time;
		}
		return qtrue;
	}

	receipt = &svs.infoReceipts[oldest];
	receipt->adr = from;
	receipt->time = svs.time;
	return qfalse;
}

/*
=================
SV_ConnectionlessPacket

A connectionless packet has four leading 0xff
characters to distinguish it from a game channel.
Clients that are in the game can still send
connectionless packets.
=================
*/
void SV_ConnectionlessPacket( netadr_t from, msg_t *msg ) {
	char	*s;
	char	*c;
	#ifdef USE_AUTH
	netadr_t	authServerIP;
	#endif

	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );		// skip the -1 marker

	if (!Q_strncmp("connect", (char *) &msg->data[4], 7)) {
		Huff_Decompress(msg, 12);
	}

	s = MSG_ReadStringLine( msg );
	Cmd_TokenizeString( s );

	c = Cmd_Argv(0);
	Com_DPrintf ("SV packet %s : %s\n", NET_AdrToString(from), c);

	if (!Q_stricmp(c, "getstatus")) {
		if (SV_CheckDRDoS(from)) { return; }
		SVC_Status( from  );
  } else if (!Q_stricmp(c, "getinfo")) {
		if (SV_CheckDRDoS(from)) { return; }
		SVC_Info( from );
	} else if (!Q_stricmp(c, "getchallenge")) {
		SV_GetChallenge( from );
	} else if (!Q_stricmp(c, "connect")) {
		SV_DirectConnect( from );
	} else if (!Q_stricmp(c, "ipAuthorize")) {
		SV_AuthorizeIpPacket( from );
	}
	#ifdef USE_AUTH
	// @Barbatos @Kalish
	else if ( (!Q_stricmp(c, "AUTH:SV")))
	{
		NET_StringToAdr(sv_authServerIP->string, &authServerIP);
		
		if ( !NET_CompareBaseAdr( from, authServerIP ) ) {
			Com_Printf( "AUTH not from the Auth Server\n" );
			return;
		}
		VM_Call(gvm, GAME_AUTHSERVER_PACKET);
	}
	#endif
	
	else if (!Q_stricmp(c, "rcon")) {
		SVC_RemoteCommand( from, msg );
	}else if (!Q_stricmp(c, "rconRecovery")) {
		SVC_RconRecoveryRemoteCommand( from, msg );
	} else if (!Q_stricmp(c, "disconnect")) {
		// if a client starts up a local server, we may see some spurious
		// server disconnect messages when their new server sees our final
		// sequenced messages to the old client
	} else {
		Com_DPrintf ("bad connectionless packet from %s:\n%s\n"
		, NET_AdrToString (from), s);
	}
}

//============================================================================

/*
=================
SV_ReadPackets
=================
*/
void SV_PacketEvent( netadr_t from, msg_t *msg ) {
	int			i;
	client_t	*cl;
	int			qport;

	// check for connectionless packet (0xffffffff) first
	if ( msg->cursize >= 4 && *(int *)msg->data == -1) {
		SV_ConnectionlessPacket( from, msg );
		return;
	}

	// read the qport out of the message so we can fix up
	// stupid address translating routers
	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );				// sequence number
	qport = MSG_ReadShort( msg ) & 0xffff;

	// find which client the message is from
	for (i=0, cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++) {
		if (cl->state == CS_FREE) {
			continue;
		}
		if ( !NET_CompareBaseAdr( from, cl->netchan.remoteAddress ) ) {
			continue;
		}
		// it is possible to have multiple clients from a single IP
		// address, so they are differentiated by the qport variable
		if (cl->netchan.qport != qport) {
			continue;
		}

		// the IP port can't be used to differentiate them, because
		// some address translating routers periodically change UDP
		// port assignments
		if (cl->netchan.remoteAddress.port != from.port) {
			Com_Printf( "SV_PacketEvent: fixing up a translated port\n" );
			cl->netchan.remoteAddress.port = from.port;
		}

		// make sure it is a valid, in sequence packet
		if (SV_Netchan_Process(cl, msg)) {
			// zombie clients still need to do the Netchan_Process
			// to make sure they don't need to retransmit the final
			// reliable message, but they don't do any other processing
			if (cl->state != CS_ZOMBIE) {
				cl->lastPacketTime = svs.time;	// don't timeout
				SV_ExecuteClientMessage( cl, msg );
			}
		}
		return;
	}
	
	// if we received a sequenced packet from an address we don't recognize,
	// send an out of band disconnect packet to it
	NET_OutOfBandPrint( NS_SERVER, from, "disconnect" );
}


/*
===================
SV_CalcPings

Updates the cl->ping variables
===================
*/
void SV_CalcPings( void ) {
	int			i, j;
	client_t	*cl;
	int			total, count;
	int			delta;
	playerState_t	*ps;

	for (i=0 ; i < sv_maxclients->integer ; i++) {
		cl = &svs.clients[i];
		if ( cl->state != CS_ACTIVE ) {
			cl->ping = 999;
			continue;
		}
		if ( !cl->gentity ) {
			cl->ping = 999;
			continue;
		}
		if ( cl->gentity->r.svFlags & SVF_BOT ) {
			cl->ping = 0;
			continue;
		}

		total = 0;
		count = 0;
		for ( j = 0 ; j < PACKET_BACKUP ; j++ ) {
			if ( cl->frames[j].messageAcked <= 0 ) {
				continue;
			}
			delta = cl->frames[j].messageAcked - cl->frames[j].messageSent;
			count++;
			total += delta;
		}
		if (!count) {
			cl->ping = 999;
		} else {
			cl->ping = total/count;
			if ( cl->ping > 999 ) {
				cl->ping = 999;
			}
		}

		// let the game dll know about the ping
		ps = SV_GameClientNum( i );
		ps->ping = cl->ping;
	}
}

/*
==================
SV_CheckTimeouts

If a packet has not been received from a client for timeout->integer
seconds, drop the conneciton.  Server time is used instead of
realtime to avoid dropping the local client while debugging.

When a client is normally dropped, the client_t goes into a zombie state
for a few seconds to make sure any final reliable message gets resent
if necessary
==================
*/
void SV_CheckTimeouts( void ) {
	int		i;
	client_t	*cl;
	int			droppoint;
	int			zombiepoint;

	droppoint = svs.time - 1000 * sv_timeout->integer;
	zombiepoint = svs.time - 1000 * sv_zombietime->integer;

	for (i=0,cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++) {
		// message times may be wrong across a changelevel
		if (cl->lastPacketTime > svs.time) {
			cl->lastPacketTime = svs.time;
		}

		if (cl->state == CS_ZOMBIE
		&& cl->lastPacketTime < zombiepoint) {
			// using the client id cause the cl->name is empty at this point
			Com_DPrintf( "Going from CS_ZOMBIE to CS_FREE for client %d\n", i );
			cl->state = CS_FREE;	// can now be reused
			continue;
		}
		if ( cl->state >= CS_CONNECTED && cl->lastPacketTime < droppoint) {
			// wait several frames so a debugger session doesn't
			// cause a timeout
			if ( ++cl->timeoutCount > 5 ) {
				SV_DropClient (cl, "timed out");
				cl->state = CS_FREE;	// don't bother with zombie state
			}
		} else {
			cl->timeoutCount = 0;
		}
	}
	
}


/*
==================
SV_CheckPaused
==================
*/
qboolean SV_CheckPaused( void ) {
	int		count;
	client_t	*cl;
	int		i;

	if ( !cl_paused->integer ) {
		return qfalse;
	}

	// only pause if there is just a single client connected
	count = 0;
	for (i=0,cl=svs.clients ; i < sv_maxclients->integer ; i++,cl++) {
		if ( cl->state >= CS_CONNECTED && cl->netchan.remoteAddress.type != NA_BOT ) {
			count++;
		}
	}

	if ( count > 1 ) {
		// don't pause
		if (sv_paused->integer)
			Cvar_Set("sv_paused", "0");
		return qfalse;
	}

	if (!sv_paused->integer)
		Cvar_Set("sv_paused", "1");
	return qtrue;
}

//UT_WEAPON_SETID(x, y) ((x) = (((x) & 0xFFFFFF00) | (0x000000FF & ((y) KEY) ) ))

qboolean same_team(client_t *cl){
	playerState_t   *ps;
	
	ps = SV_GameClientNum(cl - svs.clients);
	if (ps->persistant[PERS_TEAM] == cl->lastTeam) {return qtrue;}
	return qfalse;

}

/*
==================
check_k_weapon
==================
*/
static void check_k_weapon(client_t *cl){
    playerState_t   *ps;
    
    ps = SV_GameClientNum(cl - svs.clients);
    // case 1 (init):
	// noweap = 0
	// power 15 = 123123 (empty)
	// power 14 = 123123 (empty)
	// ->continue
	
	// case 2 (change map):
	// noweap = 123123
	// power 15 = 435345 (empty)
	// power 14 = 435345 (empty)
	// ->continue
	
	// case 3 (give all): DONE
	// noweap = 123123
	// power 15 = 756568 (weap)
	// power 14 = 135124 (weap: if 15 is a weap 14 is too)
	// ->return
	
	// case 4 (normal behave): DONE
	// noweap = 123123
	// power 15 = 123123
	// power 14 = 123123 (or another number)
	// ->return
	
    if (KEY == ps->powerups[15]) {return;} // case 4
	if (ps->powerups[15] != ps->powerups[14]) {return;} // case 3
	
	// case 1 or 2 continue
	// EXCEPTION if key=0 and power 15 is not empty...can't disarm, but should never happen except if player has gear set to 15 weap...impossible
	
    //else if((NOWEAP != 0) && (ps->powerups[15] == KNIFE || ps->powerups[15] == BERETTA)){return;}

    else{// noweap == 0 or powerups[15] != armi
        //Com_Printf("refreshing keys, noweap: %d, powerups[15]: %d\n", NOWEAP,ps->powerups[15]);
        
		KEY = ps->powerups[15];;

		KNIFE = KEY ^ 1; // slash - throw
		BERETTA = KEY ^ 2;
		DE = KEY ^ 3;
		SPAS = KEY ^ 4;
		MP5 = KEY ^ 5; // burst - auto
		UMP = KEY ^ 6; // spam - auto
		HK69 = KEY ^ 7; // short - long
		LR300 = KEY ^ 8; // burst - semi - auto
		G36 = KEY ^ 9; // burst - semi - auto
		PSG = KEY ^ 10;
		HE = KEY ^ 11;
		FLASH = KEY ^ 12;
		SMOKE = KEY ^ 13;
		SR8 = KEY ^ 14;
		AK = KEY ^ 15; // burst - semi - auto
		BOMB = KEY ^ 16; 
		NEGEV = KEY ^ 17; 
		// 18 flash flag
		M4 = KEY ^ 19; // burst - semi - auto
		GLOCK = KEY ^ 20; // burst - semi
		COLT = KEY ^ 21;
		MAC11 = KEY ^ 22;


    }
}

void set_firetime(client_t *cl, playerState_t *ps){
	//weapon = whatweaponis(ps->powerups[ps->weapon]);
	// supponendo di capire che sia un knife throw:
	// leggo il firetime desiderato e lo imposto
	// ps->weaponTime = sv_knife_throw_firetime->integer;
	//ps->weaponTime = 376;
	if (ps->weaponstate == 3){
		ps->weaponTime = 500;
		cl->bool_firetime = qfalse;
	}
}

void load_client_ps(client_t *cl){
	playerState_t	*ps;
	int				i;
	
	ps = SV_GameClientNum(cl - svs.clients);
	
	for (i=0; i<16; i++) {
		if (ps->powerups[i] = cl->weaponData[i]);
	}
}

void save_client_ps(client_t *cl){
	playerState_t	*ps;
	int				i;
	
	ps = SV_GameClientNum(cl - svs.clients);
	
	for (i=0; i<16; i++) {
		if (cl->weaponData[i] = ps->powerups[i]);
	}
}

qboolean check_client_ps(client_t *cl){
	playerState_t	*ps;
	int				i;
	
	ps = SV_GameClientNum(cl - svs.clients);
	
	for (i=0; i<16; i++) {
		if (ps->powerups[i] != cl->weaponData[i]){
			// cambio di weapon, controllo se sparato/ricaricato oppure cambio da menu
			if ((ps->powerups[i] ^ cl->weaponData[i]) % 256 != 0 ){
				// ha cambiato da menu!
				ps->powerups[i] = cl->weaponData[i];
				return qtrue;
			}
			else {
				// ha sparato/ricaricato, posso disattivare il controllo
				cl->check_ps_change = qfalse;
				return qfalse;
			}
		}
	}

	
}

void broadcastCS(){
	int				l;
	client_t		*client_search;
	
	for (l = 0, client_search = svs.clients; l < sv_maxclients->integer ; l++, client_search++) {
		if (client_search->state >= CS_CONNECTED) {
			Cmd_ExecuteString (va("scc all cs %d \"%s\"", l+544, client_search->lastCS));
		}
	}			
	
}

/*
==================
SV_ModPlayers
==================
*/
static void SV_ModPlayers(void){
    int             i,team;
    client_t        *cl;
    playerState_t   *ps;
    int             stamina, health, wj;
    int             j;
    
    if ((sv_free_stamina->integer > 0)||(sv_red_stamina->integer > 0)||(sv_blue_stamina->integer > 0)||(sv_free_walljumps->integer > 0)||(sv_red_walljumps->integer > 0)||(sv_blue_walljumps->integer > 0)) {
        for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++){
            if (cl->state != CS_ACTIVE){
                cl->countRespawn = 0;
                continue;
            }
			ps = SV_GameClientNum(i);
            team = ps->persistant[PERS_TEAM];
            
            //*********************
            // player is not spect
            //*********************
            
            if (team != TEAM_SPECTATOR) {                
                // check weapon key
                check_k_weapon(cl);
            }
            
            //********
            // free
            //********
            
            if(team == TEAM_FREE){
                if(ps->pm_type != PM_NORMAL) continue;
                
                // stamina
                if(sv_free_stamina->integer > 0){
                    stamina = *((int *)((char*)(ps->stats) + 36)); // misured on 30000
                    health = ps->stats[STAT_HEALTH]; // misured on 100
                    
                    if (sv_free_stamina->integer == 1) {
                        // set stamina always max
                        *((int *)((char*)(ps->stats) + 36)) = health * 300;
                    }
                    
                    if (sv_free_stamina->integer == 2) {
                        if (ps->velocity[0]==0 && ps->velocity[1]==0 && ps->velocity[2]==0){
                            *((int *)((char*)(ps->stats) + 36)) = health * 300;
                        }            
                    }
                    
                }
                
                // walljumps
                if(sv_free_walljumps->integer == 1){
                    ps->generic1 = 0;
                }
                
                // weapons
                if (cl->countRespawn != ps->persistant[PERS_SPAWN_COUNT]){
                    cl->countRespawn = ps->persistant[PERS_SPAWN_COUNT]; 
                    
                    if (strcmp(sv_free_weapons->string, "") != 0) {
                        int     k,n=0;
                        char    *free_weap = sv_free_weapons->string;
                        char    *weap[20];    
                        char    *temp=strdup(free_weap);
                        char    *separator = " ";
                        
                        weap[n]=strtok(temp, separator);
                        while(weap[n] && n<4) weap[++n]=strtok(NULL, separator);
                        
                        Cmd_ExecuteString (va("dis %d", i));
                        for(k=0; k<n; k++) {
                            Cmd_ExecuteString (va("gw %d \"%s\"", i, weap[k]));
                        }
						free(temp);
						
						save_client_ps(cl);
						cl->check_ps_change = qtrue;
						cl->lastTeam = TEAM_FREE;
						
						// use first weapon, FIXME: put last weapon != KEY and DROPKEY
						ps->weapon = 0;
					}
				}
				// check if weapon is changed by the user at the respawn when shouldnt be forbidden to change
				if (cl->check_ps_change == qtrue && same_team(cl)){
					if (check_client_ps(cl)){
						// FIXME: should check if the player dropped the weapons
						load_client_ps(cl);
					}
				}else{
					cl->check_ps_change = qfalse;
				}        
            }
            
            //********
            // red
            //********
            
            if(team == TEAM_RED){
                if(ps->pm_type != PM_NORMAL) continue;
                
                // stamina
                if(sv_red_stamina->integer > 0){
                    stamina = *((int *)((char*)(ps->stats) + 36)); // misured on 30000
                    health = ps->stats[STAT_HEALTH]; // misured on 100
                    
                    if (sv_red_stamina->integer == 1) {
                        // set stamina always max
                        *((int *)((char*)(ps->stats) + 36)) = health * 300;
                    }
                    
                    if (sv_red_stamina->integer == 2) {
                        if (ps->velocity[0]==0 && ps->velocity[1]==0 && ps->velocity[2]==0){
                            *((int *)((char*)(ps->stats) + 36)) = health * 300;
                        }            
                    }
                    
                }
                
                // walljumps
                if(sv_red_walljumps->integer == 1){
                    ps->generic1 = 0;
                }
                
                // weapons
                if (cl->countRespawn != ps->persistant[PERS_SPAWN_COUNT]){ // EVENT RESPAWN
                    cl->countRespawn = ps->persistant[PERS_SPAWN_COUNT]; 
                    
                    if (strcmp(sv_red_weapons->string, "") != 0) {
                        int     k,n=0;
                        char    *red_weap = sv_red_weapons->string;
                        char    *weap[20];    
                        char    *temp=strdup(red_weap);
                        char    *separator = " ";
                        
                        weap[n]=strtok(temp, separator);
                        while(weap[n] && n<4) weap[++n]=strtok(NULL, separator);
                        
                        Cmd_ExecuteString (va("dis %d", i));
                        for(k=0; k<n; k++) {
                            Cmd_ExecuteString (va("gw %d \"%s\"", i, weap[k]));
                        }
                        free(temp);

						save_client_ps(cl);
						cl->check_ps_change = qtrue;
						cl->lastTeam = TEAM_RED;
						
                        // use first weapon, FIXME: put last weapon != KEY and DROPKEY
                        ps->weapon = 0;
                    }
                }
				// check if weapon is changed by the user at the respawn when shouldnt be forbidden to change
				if (cl->check_ps_change == qtrue && same_team(cl)){
					if (check_client_ps(cl)){
						// FIXME: should check if the player dropped the weapons
						load_client_ps(cl);
					}
				}else{
					cl->check_ps_change = qfalse;
				}
				// firetime
//				if (ps->weaponstate == 0) {
//					cl->bool_firetime = qtrue;
//				}
//				if (cl->bool_firetime == qtrue) {
//					set_firetime(cl, ps);
//				}
				
				
            }
            
            //********
            // blue
            //********
            
            if(team == TEAM_BLUE){
                if(ps->pm_type != PM_NORMAL) continue;
                // stamina
                if(sv_blue_stamina->integer > 0){
                    stamina = *((int *)((char*)(ps->stats) + 36)); // misured on 30000
                    health = ps->stats[STAT_HEALTH]; // misured on 100
                    
                    if (sv_blue_stamina->integer == 1) {
                        // set stamina always max
                        *((int *)((char*)(ps->stats) + 36)) = health * 300;
                    }
                    
                    if (sv_blue_stamina->integer == 2) {
                        if (ps->velocity[0]==0 && ps->velocity[1]==0 && ps->velocity[2]==0){
                            *((int *)((char*)(ps->stats) + 36)) = health * 300;
                        }            
                    }
                    
                }
                
                // walljumps
                if(sv_blue_walljumps->integer == 1){
                    ps->generic1 = 0;
                }
                
                // weapons
                if (cl->countRespawn != ps->persistant[PERS_SPAWN_COUNT]){
                    cl->countRespawn = ps->persistant[PERS_SPAWN_COUNT]; 
                    
                    if (strcmp(sv_blue_weapons->string, "") != 0) {
                        int     k,n=0;
                        char    *blue_weap = sv_blue_weapons->string;
                        char    *weap[20];    
                        char    *temp=strdup(blue_weap);
                        char    *separator = " ";
                        
                        weap[n]=strtok(temp, separator);
                        while(weap[n] && n<4) weap[++n]=strtok(NULL, separator);
                        
                        Cmd_ExecuteString (va("dis %d", i));
                        for(k=0; k<n; k++) {
                            Cmd_ExecuteString (va("gw %d \"%s\"", i, weap[k]));
                        }
						free(temp);
						
						save_client_ps(cl);
						cl->check_ps_change = qtrue;
						cl->lastTeam = TEAM_BLUE;
						// use first weapon, FIXME: put last weapon != KEY and DROPKEY
						ps->weapon = 0;
					}
				}
				// check if weapon is changed by the user at the respawn when shouldnt be forbidden to change
				if (cl->check_ps_change == qtrue && same_team(cl)){
					if (check_client_ps(cl)){
						// FIXME: should check if the player dropped the weapons
						load_client_ps(cl);
					}
				}else{
					cl->check_ps_change = qfalse;
				}
            }
            
            // send to all colored names
			if (sv.sendcolorednames == qtrue){
				broadcastCS();
				sv.sendcolorednames = qfalse;
			}
			
            if(team == TEAM_RED){
				if (sv_test->integer ==1){
                
				
					// TEST STUFF
            
					Com_Printf("------------\n");
					//Com_Printf("weapon %d\n", ps->weapon);
					//Com_Printf("weaponstate %d\n", ps->weaponstate);
					
					            for (j=0; j<16; j++) {
					                Com_Printf("ammo: %d\n", ps->ammo[j]);
					            }

					
					if (ps->weaponstate==3){
						// firing
						//ps->weaponTime = 500;
					}
					if (ps->weaponstate != 0){
						//Com_Printf("weaponstate %d\n", ps->weaponstate);
					}
					
					
					
					//Q_strncpyz( client->lastCS, joinMessage, sizeof(joinMessage) );
					//Com_Printf("STAT_PERSISTANT_POWERUP %d\n", ps->stats[STAT_PERSISTANT_POWERUP]);
					
					
					
					
			// 0 = ready
			// 1 = raising // dropping
			// 2 = dropping
			// 3 = firing
            
            //Com_Printf("stat_weapons: %d\n",ps->stats[STAT_WEAPONS]);
//            for (j=0; j<16; j++) {
//                Com_Printf("%d\n", ps->stats[j]);
//            }
            
				}
//				if (sv_test->integer ==2){
//					Com_Printf("%d\n", ps->powerups[3]);
//				}
            }
            
            
                
            

        
            
        }
    }
    
}

/*
==================
SV_FreshWeapons
==================
*/
static void SV_FreshWeapons(void){
    
}

/*
==================
SV_GivePlayerHealth
i = client id
h = amount of health
==================
*/
void SV_GivePlayerHealth(int clId, int h) {
    char    cmd[64];
    
    //Com_sprintf(cmd, sizeof(cmd), "gh %i \"+%i\"\n", clId, h); // needs qvm mod
    //Cmd_ExecuteString(cmd);
}

/*
==================
SV_Frame

Player movement occurs as a result of packet events, which
happen before SV_Frame is called
==================
*/
void SV_Frame( int msec ) {
	int		frameMsec;
	int		startTime;

	// the menu kills the server with this cvar
	if ( sv_killserver->integer ) {
		SV_Shutdown ("Server was killed");
		Cvar_Set( "sv_killserver", "0" );
		return;
	}

	if (!com_sv_running->integer)
	{
		if(com_dedicated->integer)
		{
			// Block indefinitely until something interesting happens
			// on STDIN.
			NET_Sleep(-1);
		}
		
		return;
	}

	// allow pause if only the local client is connected
	if ( SV_CheckPaused() ) {
		return;
	}

	// if it isn't time for the next frame, do nothing
	if ( sv_fps->integer < 1 ) {
		Cvar_Set( "sv_fps", "10" );
	}

	frameMsec = 1000 / sv_fps->integer * com_timescale->value;
	// don't let it scale below 1ms
	if(frameMsec < 1)
	{
		Cvar_Set("timescale", va("%f", sv_fps->integer / 1000.0f));
		frameMsec = 1;
	}

	sv.timeResidual += msec;

	if (!com_dedicated->integer) SV_BotFrame (sv.time + sv.timeResidual);

	if ( com_dedicated->integer && sv.timeResidual < frameMsec ) {
		// NET_Sleep will give the OS time slices until either get a packet
		// or time enough for a server frame has gone by
		NET_Sleep(frameMsec - sv.timeResidual);
		return;
	}

	// if time is about to hit the 32nd bit, kick all clients
	// and clear sv.time, rather
	// than checking for negative time wraparound everywhere.
	// 2giga-milliseconds = 23 days, so it won't be too often
	if ( svs.time > 0x70000000 ) {
		SV_Shutdown( "Restarting server due to time wrapping" );
		Cbuf_AddText( va( "map %s\n", Cvar_VariableString( "mapname" ) ) );
		return;
	}
	// this can happen considerably earlier when lots of clients play and the map doesn't change
	if ( svs.nextSnapshotEntities >= 0x7FFFFFFE - svs.numSnapshotEntities ) {
		SV_Shutdown( "Restarting server due to numSnapshotEntities wrapping" );
		Cbuf_AddText( va( "map %s\n", Cvar_VariableString( "mapname" ) ) );
		return;
	}

	if( sv.restartTime && sv.time >= sv.restartTime ) {
		sv.restartTime = 0;
		Cbuf_AddText( "map_restart 0\n" );
		return;
	}

	// update infostrings if anything has been changed
	if ( cvar_modifiedFlags & CVAR_SERVERINFO ) {
		SV_SetConfigstring( CS_SERVERINFO, Cvar_InfoString( CVAR_SERVERINFO ) );
		cvar_modifiedFlags &= ~CVAR_SERVERINFO;
	}
	if ( cvar_modifiedFlags & CVAR_SYSTEMINFO ) {
		SV_SetConfigstring( CS_SYSTEMINFO, Cvar_InfoString_Big( CVAR_SYSTEMINFO ) );
		cvar_modifiedFlags &= ~CVAR_SYSTEMINFO;
	}

	if ( com_speeds->integer ) {
		startTime = Sys_Milliseconds ();
	} else {
		startTime = 0;	// quite a compiler warning
	}

	// update ping based on the all received frames
	SV_CalcPings();

	if (com_dedicated->integer) SV_BotFrame (sv.time);

	// run the game simulation in chunks
	while ( sv.timeResidual >= frameMsec ) {
		sv.timeResidual -= frameMsec;
		svs.time += frameMsec;
		sv.time += frameMsec;

		// let everything in the world think and move
		VM_Call (gvm, GAME_RUN_FRAME, sv.time);
	}

	if ( com_speeds->integer ) {
		time_game = Sys_Milliseconds () - startTime;
	}
    if (sv_mod->integer > 0){
        SV_ModPlayers();
    }
    
    SV_FreshWeapons();
    
	// check timeouts
	SV_CheckTimeouts();
	
	// check user info buffer thingy
	SV_CheckClientUserinfoTimer();

	// send messages back to the clients
	SV_SendClientMessages();

	// send a heartbeat to the master if needed
	SV_MasterHeartbeat();
}

//============================================================================

