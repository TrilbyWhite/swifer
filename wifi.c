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

#define MODE_AUTO	0x0001
#define MODE_ANY	0x0002
#define MODE_ADD	0x0010
#define MODE_RECON	0x0100

static int create_netfile(wireless_scan *,int);
static int draw(wireless_scan *, int);
static int init_curses();
static int is_known(wireless_scan *);
static wireless_scan *get_best();
static int refresh_list();
static wireless_scan *select_network();
static wireless_scan *show_menu();
static int secure_connect(wireless_scan *);
static int simple_connect(wireless_scan *);

static const char *netpath = "/etc/wifi/networks";

static char ifname[IFNAMSIZ+1] = "wlan0";
static char *netfile = NULL;
static int we_ver, skfd, mode;
static wireless_scan_head context;
static wireless_config cur;

int create_netfile(wireless_scan *ws,int sec) {
	char *cmd = NULL;
	FILE *fnet = fopen(netfile,"w");
	fprintf(fnet,"## Connection script for \"%s\"\n\n",ws->b.essid);
	fprintf(fnet,"killall dhcpcd && sleep 0.5\nip link set %s up\n",ifname);
	if (sec) {
		fprintf(fnet,"killall wpa_supplicant && sleep 0.5\n");
		fprintf(fnet,"cat <<EOF > wifiTMP\n");
		fclose(fnet);
		char psk[64];
		fprintf(stdout,"Enter passkey for \"%s\"\n> ",ws->b.essid);
		fflush(stdout);
		scanf("%s",psk);
		cmd = (char *) calloc(strlen(ws->b.essid) +
			strlen(psk) + strlen(netfile) + 35,sizeof(char));
		sprintf(cmd,"wpa_passphrase \"%s\" \"%s\" >> \"%s\"",
				ws->b.essid,psk,netfile);
		system(cmd);
		fnet = fopen(netfile,"a");
		fprintf(fnet,"EOF\nwpa_supplicant -B -i%s -c wifiTMP\n",ifname);
		fprintf(fnet,"rm wifiTMP\ndhcpcd %s\n\n",ifname);
	}
	else {
		fprintf(fnet,"iwconfig %s essid \"%s\"\n",ifname,ws->b.essid);
		fprintf(fnet,"dhcpcd %s\n\n",ifname);
	}
	fclose(fnet);
	if (cmd) free(cmd);
} 

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
	if (!ws) return False;
	if (strlen(ws->b.essid) < 1) return 0;
	char *t = (char *) calloc(strlen(ws->b.essid) +
			strlen(netpath) + 4, sizeof(char));
	sprintf(t,"%s/%s",netpath,ws->b.essid);
	int known = open(t,O_RDONLY);
	free(t);
	if (known > 0)
		close(known);
	else
		known = 0;
	return known;
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

static void connect_helper(wireless_scan *ws) {
	/* connect: */
	char *cmd = (char *) calloc(strlen(netfile)+26,sizeof(char));
	sprintf(cmd,"sh \"%s\" >/dev/null 2>&1",netfile);
	system(cmd);
	/* save netfile: */
	if (!is_known(ws) && (mode & MODE_ADD) ) {
		char *cmd = (char *) realloc(cmd,(strlen(netfile) +
			strlen(netpath) + strlen(ws->b.essid) + 15) * sizeof(char));
		sprintf(cmd,"cp \"%s\" \"%s/%s\"",netfile,netpath,ws->b.essid);
		system(cmd);
		free(cmd);
	}
	else if (!is_known(ws) || (mode & MODE_ADD)) {
		sprintf(cmd,"rm \"%s\"",netfile);
		system(cmd);
	}
	free(cmd);
}

int secure_connect(wireless_scan *ws) {
	char keyfile[255];
	netfile = (char *) calloc(strlen(ws->b.essid)+14,sizeof(char));
	sprintf(netfile,"/tmp/wifi_%s",ws->b.essid);
	create_netfile(ws,True);
	connect_helper(ws);
}

int simple_connect(wireless_scan *ws) {
	if (is_known(ws)) {
		netfile = (char *) calloc(strlen(ws->b.essid) +
				strlen(netpath) + 4, sizeof(char));
		sprintf(netfile,"%s/%s",netpath,ws->b.essid);
	}
	else {
		netfile = (char *) calloc(strlen(ws->b.essid)+14,sizeof(char));
		sprintf(netfile,"/tmp/wifi_%s",ws->b.essid);
		create_netfile(ws,False);
	}
	connect_helper(ws);
}

int main(int argc, const char **argv) {
	char *cmd = (char *) calloc(strlen(ifname)+20,sizeof(char));
	sprintf(cmd,"ip link set %s up",ifname);
	system(cmd);
	free(cmd);
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
	if (is_known(ws) || (ws->b.key_flags != 2048)) simple_connect(ws);
	else secure_connect(ws);
	if (netfile) {
		free(netfile);
		netfile = NULL;
	}
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

