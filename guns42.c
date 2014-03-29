char weaponforpistol[11] = { 'G', 'H', 'D', 'S', 'J', 'E', 'O', 'Q', 'I', 'N', 'F' };
char items[5] = { 'D', 'E', 'C', 'A', 'F' };
char pistols[2] = { 'B', 'C' }; 
int amo[11] = { 100, 50, 40, 10, 20, 30, 60, 80, 70, 90, 255 };
int nades[12] = { 5, 8, 10, 12, 30, 15, 18, 12, 7, 20, 9, 50 };
int health[8] = { 20, 30, 40, 50, 75, 80, 100, 100 };
char healthops[2] = { '+', '-' };

char SV_GetRandomWeapon( client_t *cl ) {
    char        weapon;
    int         random;
    int         i;
    int         qtrues;

    qtrues = 0;
        
    for ( i = 0; i < 11; i++ ) {
        if ( cl->weapongivenforpistol[i] == qtrue ) {
            qtrues++;
        }
    }
        
    if ( qtrues == 11 ) {
        for ( i = 0; i < 11; i++ ) {
            cl->weapongivenforpistol[i] = qfalse;
        }
    }
    
    //Pistol Kill
    TRYAGAINPISTOL:
        
        random = rand() % 11;
        // If you already have the weap do a new random
        if ( cl->weapongivenforpistol[random] == qtrue ) {
            goto TRYAGAINPISTOL;
        }
        else {
            weapon = weaponforpistol[random];
            cl->weapongivenforpistol[random] = qtrue;
            return weapon;
        }
}

char SV_GetRandomItem( client_t *cl ) {
    char        item;
    int         random;
    int         i;
    int         qtrues;

    qtrues = 0;
        
    for ( i = 0; i < 5; i++ ) {
        if ( cl->gunitems[i] == qtrue ) {
            qtrues++;
        }
    }
        
    if ( qtrues == 5 ) {
        for ( i = 0; i < 5; i++ ) {
            cl->gunitems[i] = qfalse;
        }
    }

    
    TRYAGAINITEM:

        random = rand() % 5;
        // If you already have the item do a new item
        if ( cl->gunitems[random] == qtrue ) {
            goto TRYAGAINITEM;
        }
        else {
            item = items[random];
            cl->gunitems[random] = qfalse;
            return item;
        }
}

char *SV_NameWeapon(weap2) {
    char *weapon = " ";
    if (weap2=='N') {
        weapon="^6Sr8";
    }
    else if (weap2=='O') {
        weapon="^5AK103";
    }
    else if (weap2=='Q') {
        weapon="^4NEGEV";
    }
    else if (weap2=='F') {
        weapon="^3UMP45";
    }
    else if (weap2=='I') {
        weapon="^5G36";
    }
    else if (weap2=='G') {
        weapon="^1HK69";
    }
    else if (weap2=='H') {
        weapon="^5LR300";
    }
    else if (weap2=='D') {
        weapon="^3Spas";
    }
    else if (weap2=='S') {
        weapon="^5M4A1";
    }
    else if (weap2=='J') {
        weapon="^6PSG1";
    }
    else if (weap2=='E') {
        weapon="^3MP5K";
    }
    else if (weap2=='B') {
        weapon="^2Beretta";
    }
    else if (weap2=='C') {
        weapon="^2Desert Eagle";
    }
    else if (weap2=='K') {
        weapon="^1HE Grenades";
    }
    return weapon;
}

char *SV_NameItem(item2) {
    char *item = " ";
    if (item2=='D') {
        item="Silencer";
    }
    else if (item2=='E') {
        item="Laser Sight";
    }
    else if (item2=='C') {
        item="Ultra Medkit";
    }
    else if (item2=='A') {
        item="Kevlar";
    }
    else if (item2=='F') {
        item="Helmet";
    }
    return item;
}

void SV_ClientSpawn( int clID ) {
    client_t        *cl;
    playerState_t   *ps;
    int             i;

    cl = &svs.clients[ clID ];
    ps = SV_GameClientNum( clID );
    
    // IF client spawn all the "have weapon" qboolean are set to qfalse
    for ( i = 0; i < 11; i++ ) {
        cl->weapongivenforpistol[i] = qfalse;
        if (i < 5) {
            cl->gunitems[i] = qfalse;
        }
    }
}

void SV_Event_Kill( char *killer, char *killed, char *wpn ) {
    client_t        *clkilled;
    client_t        *clkiller;
    playerState_t   *pskilled;
    playerState_t   *pskiller;

    int skiller = atoi( killer );
    clkilled = &svs.clients[ atoi(killed) ];
    clkiller = &svs.clients[ atoi(killer) ];
    pskilled = SV_GameClientNum( atoi(killed) );
    pskiller = SV_GameClientNum( atoi(killer) );
    
    // If the killer is not the world
    SV_ClientSpawn( atoi( killed ) );
    if ( atoi(killer) != -1 ) {
    
        // If the killer is not the killed (suicide)
        if ( atoi(killer) != atoi(killed) ) {
            // Knife
            if (!Q_stricmp( wpn, "12:" )) {
                int health2 = health[rand()%8];
                Cmd_ExecuteString (va("gh %i +%i", skiller, health2));
                SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^1Knife ^7kill ^1= ^7Health: ^2+%i\"", health2);
            }
            if (!Q_stricmp( wpn, "13:" )) {
                char item = SV_GetRandomItem( clkiller );
                Cmd_ExecuteString (va("gi %i %c", skiller, item));
                SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^1Throwing Knife ^7kill ^1= ^6%s\"", SV_NameItem(item));
            }
            // Beretta
            if (!Q_stricmp( wpn, "14:" )) {
                char weapon = SV_GetRandomWeapon( clkiller );
                Cmd_ExecuteString (va("gw %i %c", skiller, weapon));
                SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^2Beretta ^7kill ^1= %s\"", SV_NameWeapon(weapon));
            }
            // Desert Eagle
            else if (!Q_stricmp( wpn, "15:" )) {
                char weapon = SV_GetRandomWeapon( clkiller );
                Cmd_ExecuteString (va("gw %i %c", skiller, weapon));
                SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^2Desert Eagle ^7kill ^1= %s\"", SV_NameWeapon(weapon));
            }
            // Spas
            else if (!Q_stricmp( wpn, "16:" )) {
                char weapon = weaponforpistol[rand() % 11];
                int amo2 = amo[rand()%11];
                Cmd_ExecuteString (va("gw %i %c +%i", skiller, weapon, amo2));
                SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^3Spas ^7kill ^1= %s ^4+%i\"", SV_NameWeapon(weapon), amo2);
            }
            // UMP45
            else if (!Q_stricmp( wpn, "17:" )) {
                char weapon = 'B';
                Cmd_ExecuteString (va("gw %i %c +30", skiller, weapon));
                SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^3UMP45 ^7kill ^1= %s ^4+30\"", SV_NameWeapon(weapon));
            }
            // MP5K
            else if (!Q_stricmp( wpn, "18:" )) {
                char weapon = 'C';
                Cmd_ExecuteString (va("gw %i %c +15", skiller, weapon));
                SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^3MP5K ^7kill ^1= %s ^4+15\"", SV_NameWeapon(weapon));
            }
            // LR
            // else if (!Q_stricmp( wpn, "19:" )) {

            // }
            // G36
            // else if (!Q_stricmp( wpn, "20:" )) {

            // }
            // PSG1
            else if (!Q_stricmp( wpn, "21:" )) {
                char weapon = 'K';
                int nades2 = nades[rand()%12];
                Cmd_ExecuteString (va("gw %i %c +%i", skiller, weapon, nades2));
                SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^6PSG1 ^7kill ^1= %s ^4+%i\"", SV_NameWeapon(weapon), nades2);
            }
            // HK69
            else if ((!Q_stricmp( wpn, "22:" )) || (!Q_stricmp( wpn, "37:" ))) {
                int health2 = health[rand()%8];
                char operator = healthops[rand()%2];
                Cmd_ExecuteString (va("gh %i %c%i", skiller, operator, health2));
                if (operator == '-') {
                    SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^1HK69 ^7kill ^1= ^7Random Health: ^1-%i\"", health2);
                }
                else if (operator == '+') {
                    SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^1HK69 ^7kill ^1= ^7Random Health: ^2+%i\"", health2);
                }       
            }
            // BLEED
            else if (!Q_stricmp( wpn, "23:" )) {
                char item = SV_GetRandomItem( clkiller );
                Cmd_ExecuteString (va("gi %i %c", skiller, item));
                SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^1Bleeding ^7kill ^1= ^6%s\"", SV_NameItem(item));
            }
            // BOOT (KICKED)
            else if (!Q_stricmp( wpn, "24:" )) {
                int health2 = health[rand()%8];
                Cmd_ExecuteString (va("gh %i +%i", skiller, health2));
                SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^6Boot ^7kill ^1= ^7Health: ^2+%i\"", health2);
            }
            // HE NADE
            else if (!Q_stricmp( wpn, "25:" )) {
                char item = SV_GetRandomItem( clkiller );
                Cmd_ExecuteString (va("gi %i %c", skiller, item));
                SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^1HE Grenade ^7kill ^1= ^6%s\"", SV_NameItem(item));
            }
            // SR8
            else if (!Q_stricmp( wpn, "28:" )) {
                char weapon = 'K';
                int nades2 = nades[rand()%12];
                Cmd_ExecuteString (va("gw %i %c +%i", skiller, weapon, nades2));
                SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^6Sr8 ^7kill ^1= %s ^4+%i\"", SV_NameWeapon(weapon), nades2);
            }
            // AK103
            // else if (!Q_stricmp( wpn, "30:" )) {

            // }
            // NEGEV
            else if (!Q_stricmp( wpn, "35:" )) {
                int health2 = health[rand()%8];
                char operator = healthops[rand()%2];
                Cmd_ExecuteString (va("gh %i %c%i", skiller, operator, health2));
                if (operator == '-') {
                    SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^4NEGEV ^7kill ^1= ^7Random Health: ^1-%i\"", health2);
                }
                else if (operator == '+') {
                    SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^4NEGEV ^7kill ^1= ^7Random Health: ^2+%i\"", health2);
                }
            }
            // M4
            // else if (!Q_stricmp( wpn, "38:" )) {

            // }
            // CURB (GOOMBA)
            else if (!Q_stricmp( wpn, "40:" )) {
                int i;
                for ( i = 0; i < 11; i++ ) {
                    Cmd_ExecuteString (va("gw %i %c 100 100", skiller, weaponforpistol[i]));
                    if (i < 2) {
                        Cmd_ExecuteString (va("gw %i %c 100 100", skiller, pistols[i]));
                    }
                }
                SV_SendServerCommand(clkiller, "chat \"^7[^4Guns^7] ^6Curb Stomp ^7kill ^1= ^5All Weapons with ^4100^7 Bullets\"");
                Cmd_ExecuteString (va("bigtext \"%s made a ^6Curb Stomp^7!! he won ^5All Weapons ^7with ^4100 ^7bullets!!!\"", clkiller->name));
            }
        }
    }
}

void SV_FlagTaken( char *client ) {
    client_t        *clclient;
    playerState_t   *psclient;

    clclient = &svs.clients[ atoi(client) ];
    int sclient = atoi( client );
    char weapon = weaponforpistol[rand() % 11];
    int amo2 = amo[rand()%11];
    Cmd_ExecuteString (va("gw %i +%c-@", sclient, weapon));
    SV_SendServerCommand(clclient, "chat \"^7[^4Guns^7] ^5Flag ^7Taken! You lost your weapons and won: %s ^4+%i\"", SV_NameWeapon(weapon), amo2);
    Cmd_ExecuteString (va("gw %i %c %i 0", sclient, weapon, amo2));
}

void SV_FlagCaptured( char *client ) {
    client_t        *clclient;
    playerState_t   *psclient;

    int sclient = atoi( client );
    clclient = &svs.clients[ atoi(client) ];
    char weapon = pistols[rand() % 2];
    int amo2 = amo[rand()%11];
    Cmd_ExecuteString (va("gw %i %c +%i", sclient, weapon, amo2));
    Cmd_ExecuteString (va("gw %i +A", sclient));
    SV_SendServerCommand(clclient, "chat \"^7[^4Guns^7] ^5Flag ^7Captured! You won: %s ^4+%i\"", SV_NameWeapon(weapon), amo2);
}

void Check_Com_Printf ( const char *text ) {
    static char data[BIG_INFO_STRING];
    
    data[0] = '\0';
    Q_strncpyz( data, text, sizeof( data ) );
    
    Cmd_TokenizeString(data);
    
    if( !Q_stricmp( Cmd_Argv(0), "Kill:" ) ) {
        SV_Event_Kill( Cmd_Argv(1), Cmd_Argv(2), Cmd_Argv(3) );
    }
    else if( !Q_stricmp( Cmd_Argv(0), "ClientSpawn:" ) ) {
        SV_ClientSpawn( atoi( Cmd_Argv(1) ) );
    }
    else if( (!Q_stricmp( Cmd_Argv(0), "Item:" ) && !Q_stricmp( Cmd_Argv(2), "team_CTF_redflag")) || (!Q_stricmp( Cmd_Argv(0), "Item:" ) && !Q_stricmp( Cmd_Argv(2), "team_CTF_blueflag")) ) {
        SV_FlagTaken( Cmd_Argv(1) );
    }
    else if(!Q_stricmp( Cmd_Argv(0), "Flag:" ) && !Q_stricmp( Cmd_Argv(2), "2:") ) {
        SV_FlagCaptured( Cmd_Argv(1) );
    }
    
    Com_Printf( text );
}