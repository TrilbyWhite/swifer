/****************************************************************************\
* WIFI - wifi connector with automatic connection modes
* By: Jesse McClure, copyright 2012
* License: GPLv3
* USAGE: wifi [ auto | any | add | reconnect ]
\****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ncurses.h>
#include <iwlib.h>

#define True	1
#define False	0

#define ELEN		IW_ESSID_MAX_SIZE
#define TIMEOUT		5
#define THRESHOLD	10
#define MAX_LINE	255

#define MODE_AUTO	0x0001
#define MODE_ANY	0x0002
#define MODE_ADD	0x0010
#define MODE_RECON	0x0100
#define MODE_SEC	0x1000

static int draw(wireless_scan *, int);
static int init_curses();
static int is_known(wireless_scan *);
static wireless_scan *get_best();
static int refresh_list();
static wireless_scan *select_network();
static wireless_scan *show_menu();
static int ws_connect(wireless_scan *);

static char ifname[IFNAMSIZ+1] = "wlan0";
static const char *config = "/etc/wifi.conf";

static int we_ver, skfd, mode;
static wireless_scan_head context;
static wireless_config cur;

static char cmd[MAX_LINE] = "";

int draw(wireless_scan *ws,int sel) {
	int perc = 100 * ws->stats.qual.qual / 64;
	attron(COLOR_PAIR(2));
	if (sel) attron(A_REVERSE);
	if (ws->b.key_flags == 2048) attron(COLOR_PAIR(9));
	else attron(COLOR_PAIR(10));
	printw(" %-*s ",ELEN+2,ws->b.essid);
	if (sel) attroff(A_REVERSE);
	if (perc > 96) attron(COLOR_PAIR(4));
	else if (perc > 84) attron(COLOR_PAIR(5));
	else if (perc > 69) attron(COLOR_PAIR(6));
	else if (perc > 44) attron(COLOR_PAIR(7));
	else attron(COLOR_PAIR(8));
	printw("%3d%%",perc);
	attron(COLOR_PAIR(3));
	printw("  %3u,%-2u ",ws->stats.qual.level, ws->stats.qual.noise);
	if (strncmp(cur.essid,ws->b.essid,ELEN)==0) attron(A_REVERSE);
	printw(" 00:00:00:00:00 \n");
	if (strncmp(cur.essid,ws->b.essid,ELEN)==0) attroff(A_REVERSE);
}

int init_curses() {
	initscr();
	raw();
	noecho();
	curs_set(0);
	start_color();
	use_default_colors();
	init_pair(1,232,4);
	init_pair(2,11,-1);
	init_pair(3,8,-1);
	init_pair(4,46,-1);
	init_pair(5,40,-1);
	init_pair(6,34,-1);
	init_pair(7,28,-1);
	init_pair(8,22,-1);
	init_pair(9,9,-1);
	init_pair(10,12,-1);
}

int is_known(wireless_scan *ws) {
	FILE *cfg = fopen(config,"r");
	char line[MAX_LINE+1];
	while ( (fgets(line,MAX_LINE,cfg)) != NULL)
		if (strncmp(line,"[NETWORKS]",10) == 0)
			break;
	while ( (fgets(line,MAX_LINE,cfg)) != NULL)
		if (strncmp(line,ws->b.essid,strlen(ws->b.essid)) == 0) {
			fclose(cfg);
			return True;
		}
	fclose(cfg);
	return False;
}

wireless_scan *get_best() {
	wireless_scan *best=NULL,*ws;
	unsigned short int known, sec, qual;
	for (ws = context.result; ws; ws = ws->next) {
		if (strlen(ws->b.essid) == 0) continue;
		known = is_known(ws);
		sec = (ws->b.key_flags == 2048);
		qual = ( best ? (ws->stats.qual.qual > best->stats.qual.qual) : False);
		if ( (!best && known) || (!best && (mode&MODE_ANY) && !sec) ||
			(qual && known) || (qual && (mode&MODE_ANY) && !sec))
			best = ws;
	}
	return best;
}

int refresh_list() {
	clear();
	attron(COLOR_PAIR(10));
	printw("\n\n  Getting wifi listing ...\n");
	refresh();
	iw_scan(skfd,ifname,we_ver,&context);
	wireless_scan *ws;
	int n;
	for (ws = context.result, n=0; ws; ws = ws->next)
		if (strlen(ws->b.essid) >= 1) n++;
	clear();
	return n-1;
}

wireless_scan *select_network() {
	wireless_scan *ws;
	int running = True;
	int c,i,sel = 0;
	int n = refresh_list();
	while (running) {
		move(0,0);
		attron(COLOR_PAIR(1));
		printw(" %-*s  %%      l,n   BSSID          \n",ELEN+2,"Network"); 
		i = 0;
		ws = context.result;
		while (ws) {
			if (strlen(ws->b.essid) >= 1) draw(ws,(i==sel));
			ws = ws->next;
			i++;
		}
		attron(COLOR_PAIR(1));
		printw(" %-*s \n",ELEN+31," "); 
		refresh();
		c = getchar();
		if (c == 'q') running = False;
		else if (c == 13) break;
		else if (c == 'r') n = refresh_list();
		else if (c == 'j' || c == 66) sel++;
		else if (c == 'k' || c == 65) sel--;
		if (sel >= n) sel = n;
		else if (sel < 0) sel = 0;
	}
	if (!running) {
		endwin();
		iw_sockets_close(skfd);
		exit(0);
	}
	for (i = 0, ws = context.result; i != sel; i++, ws = ws->next);
	return ws;
}

wireless_scan *show_menu() {
	init_curses();
	wireless_scan *ws = select_network();
	endwin();
	return ws;
}

int ws_connect(wireless_scan *ws) {
	if ( !is_known(ws) && (mode & MODE_SEC)) { /* secure unknown network */
		char psk[64];
		fprintf(stdout,"Enter passkey for \"%s\"\n> ",ws->b.essid);
		fflush(stdout);
		scanf("%s",psk);
		sprintf(cmd,"wpa_passphrase \"%s\" \"%s\" >> /etc/wpa_supplicant.conf",
			ws->b.essid,psk);
		system(cmd);
	}
	if (mode & MODE_SEC) { /* secure known/new */
		sprintf(cmd,"wpa_supplicant -B -i%s -c/etc/wpa_supplicant.conf",ifname);
		system(cmd);
	}
	else {	/* unsecure network */
		struct iwreq req;
		req.u.essid.flags = 1;
		req.u.essid.pointer = (caddr_t) ws->b.essid;
		req.u.essid.length = strlen(ws->b.essid);
		if (we_ver < 21) req.u.essid.length++;
		iw_set_ext(skfd,ifname,SIOCSIWESSID,&req);
	}
	if (!is_known(ws) && (mode & MODE_ADD))	{
		FILE *cfg = fopen(config,"a");
		fprintf(cfg,"%s\n",ws->b.essid);
		fclose(cfg);
	}
	sprintf(cmd,"dhcpcd %s",ifname); system(cmd);
}

int main(int argc, const char **argv) {
	FILE *cfg;
	if ( (cfg=fopen(config,"r")) ) {
		fscanf(cfg,"INTERFACE: %s\n",ifname);
		fclose(cfg);
	}
	sprintf(cmd,"ip link set %s up",ifname); system(cmd);
	we_ver = iw_get_kernel_we_version();
	skfd = iw_sockets_open();
	iw_get_basic_config(skfd,ifname,&cur);
	int i;
	for (i = 1; i < argc; i++) {
		if (strncmp(argv[i],"ad",2)==0) mode |= MODE_ADD;
		else if (strncmp(argv[i],"au",2)==0) mode |= MODE_AUTO;
		else if (strncmp(argv[i],"an",2)==0) mode |= (MODE_ANY & MODE_AUTO);
		else if (strncmp(argv[i],"re",2)==0) mode |= MODE_RECON;
		else fprintf(stderr,"[%s] Ignoring unknown parameter: %s\n",
			argv[0],argv[i]);
	}
	iw_scan(skfd,ifname,we_ver,&context);
	wireless_scan *ws;
	if (mode & MODE_AUTO) ws = get_best();
	else ws = show_menu();
	if (!ws) {
		iw_sockets_close(skfd);
		fprintf(stderr,"[wifi] no suitable networks found.\n");
		return 1;
	}
	system("killall dhcpcd > /dev/null 2>&1 && sleep 0.5");
	system("killall wpa_supplicant > /dev/null 2>&1 && sleep 0.5");
	if ( (mode & MODE_ADD) && is_known(ws) ) mode &= ~MODE_ADD;
	if (ws->b.key_flags == 2048) mode |= MODE_SEC;
	ws_connect(ws);
	if (mode & MODE_RECON & MODE_AUTO) {
		while (True /*TODO: connected? */) sleep(TIMEOUT);
		iw_sockets_close(skfd);
		return main(argc,argv);
	}
	else if (mode & MODE_RECON)
		fprintf(stderr,"[%s] reconnect not yet implemented for manual modes.\n",
			argv[0]); 
	iw_sockets_close(skfd);
	return 0;
}

