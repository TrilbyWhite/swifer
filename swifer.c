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

#define TIMEOUT		5
#define THRESHOLD	10
#define MAX_LINE	255
#define DHCPLEN		24

#define MODE_AUTO		0x0001
#define MODE_ANY		0x0002
#define MODE_ADD		0x0010
#define MODE_RECONNECT	0x0100
#define MODE_SECURE		0x1000
#define MODE_VERBOSE	0x2000

static int draw_entry(wireless_scan *, int);
static int is_known(wireless_scan *);
static wireless_scan *get_best();
static int refresh_list();
static wireless_scan *show_menu();
static int spawn(const char *,const char *);
static int ws_connect(wireless_scan *);

static char ifname[IFNAMSIZ+1] = "wlan0";
static const char *config = "/etc/swifer.conf";
static const char *netpath = "/usr/swifer/";
static int we_ver, skfd, mode;
static wireless_scan_head context;
static wireless_config cur;
static char cmd[MAX_LINE] = "";
static char dhcp[DHCPLEN] = "dhcpcd";

int draw_entry(wireless_scan *ws,int sel) {
	/* known and/or currently connected */
	attron(COLOR_PAIR(2));
	if (strncmp(cur.essid,ws->b.essid,IW_ESSID_MAX_SIZE)==0) attron(A_REVERSE);
	if (is_known(ws)) printw("*");
	else printw(" ");
	if (strncmp(cur.essid,ws->b.essid,IW_ESSID_MAX_SIZE)==0) attroff(A_REVERSE);
	/* ESSID & selection cursor */
	if (sel) attron(A_REVERSE);
	if (ws->b.key_flags == 2048) attron(COLOR_PAIR(4));
	else attron(COLOR_PAIR(3));
	printw(" %-*s ",IW_ESSID_MAX_SIZE+2,ws->b.essid);
	if (sel) attroff(A_REVERSE);
	/* Connection strength */
	int perc = 100 * ws->stats.qual.qual / 64;
	if (perc > 94) attron(COLOR_PAIR(5));
	else if (perc > 84) attron(COLOR_PAIR(6));
	else if (perc > 64) attron(COLOR_PAIR(7));
	else attron(COLOR_PAIR(8));
	printw("%3d%%\n",perc);
}

int is_known(wireless_scan *ws) {
	FILE *cfg = fopen(config,"r");
	if (!cfg) return False;
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
		qual = ( best ? (ws->stats.qual.qual > best->stats.qual.qual) : True);
		if ( (qual && known) || (qual && (mode&MODE_ANY) && !sec) )
			best = ws;
	}
	return best;
}

int refresh_list() {
	clear();
	attron(COLOR_PAIR(2));
	printw("\n\n  Getting wifi listing ...\n");
	refresh();
	iw_scan(skfd,ifname,we_ver,&context);
	wireless_scan *ws;
	int n;
	for (ws = context.result, n=0; ws; ws = ws->next)
		if (strlen(ws->b.essid) > 0) n++;
	clear();
	return n-1;
}

wireless_scan *show_menu() {
	/* Init ncurses */
	initscr(); raw(); noecho(); curs_set(0);
	start_color(); use_default_colors();
	init_pair(1,232,4); init_pair(2,11,-1); init_pair(3,12,-1); init_pair(4,9,-1);
	init_pair(5,46,-1); init_pair(6,40,-1); init_pair(7,28,-1); init_pair(8,22,-1);
	/* Select entry */
	wireless_scan *ws;
	int running = True;
	int c,i,sel = 0;
	int n = refresh_list();
	while (running) {
		move(0,0);
		attron(COLOR_PAIR(1));
		printw("* %-*s   %%  \n",IW_ESSID_MAX_SIZE+2,"Network"); 
		for (ws = context.result, i=0; ws; ws = ws->next, i++)
			if (strlen(ws->b.essid) >= 1) draw_entry(ws,(i==sel));
		attron(COLOR_PAIR(1));
		printw(" %-*s \n",IW_ESSID_MAX_SIZE+8," "); 
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
	if (!running) {	/* "q" selected */
		endwin();
		iw_sockets_close(skfd);
		exit(0);
	}
	for (i = 0, ws = context.result; i != sel; i++, ws = ws->next);
	/* End ncurses session & return result */
	endwin();
	return ws;
}

int spawn(const char *proc,const char *netfile) {
	if (fork() != 0) return 0;
	const char *args[7];
	args[0] = proc;
	if (netfile) {	
		args[1] = "-B"; args[2] = "-i"; args[3] = ifname;
		args[4] = "-c"; args[5] = netfile; args[6] = NULL;
	}
	else {
		args[1] = ifname; args[2] = NULL;
	}
	setsid(); fclose(stderr); fclose(stdout);
	execvp(args[0],(char * const *)args);
}

int ws_connect(wireless_scan *ws) {
	char *netfile = NULL;
	if (mode & MODE_SECURE) {
		netfile = (char *) calloc(strlen(netpath)+strlen(ws->b.essid)+2,
			sizeof(char));
		strcpy(netfile,netpath);
		strcat(netfile,ws->b.essid);
	}
	if ( !is_known(ws) && (mode & MODE_SECURE)) { /* secure unknown network */
		char psk[64];
		fprintf(stdout,"Enter passkey for \"%s\"\n> ",ws->b.essid);
		fflush(stdout);
		scanf("%s",psk);
		sprintf(cmd,"wpa_passphrase \"%s\" \"%s\" > %s",ws->b.essid,psk,netfile);
		system(cmd);
	}
	if (mode & MODE_SECURE) { /* secure known/new */
		spawn("wpa_supplicant",netfile);
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
		FILE *cfg;
		if ( (cfg=fopen(config,"ax")) ) /* no config file, create new */
			fprintf(cfg,"\n[NETWORKS]\n%s\n",ws->b.essid);
		else if ( (cfg=fopen(config,"a")) ) /* normal append */
			fprintf(cfg,"%s\n",ws->b.essid);
		if (cfg) fclose(cfg);
	}
	else if (!is_known(ws) && (mode & MODE_SECURE)) {
		sprintf(cmd,"rm %s",netfile);
		system(cmd);
	}	
	if (netfile) {
		free(netfile);
		netfile = NULL;
	}
	spawn(dhcp,netfile);
}

int main(int argc, const char **argv) {
	/* Check uid */
	if (getuid() != 0) {
		fprintf(stderr,"Swifer must be run as root.\n");
		return 1;
	}
	/* Check config file for interface and dhcp */
	FILE *cfg;
	if ( (cfg=fopen(config,"r")) ) {
		char *line = calloc(MAX_LINE+1,sizeof(char));
		char *val = calloc(MAX_LINE+1,sizeof(char));
		while (fgets(line,MAX_LINE,cfg) != NULL) {
			if (sscanf(line,"INTERFACE = %s",val))
				strncpy(ifname,val,IFNAMSIZ);
			else if (sscanf(line,"DHCP = %s",val))
				strncpy(dhcp,val,DHCPLEN);
			else if (strncmp(line,"[NETWORKS]",10))
				break;
		}
		free(line); free(val); fclose(cfg);
	}
	/* Get basic wifi info */
	we_ver = iw_get_kernel_we_version();
	skfd = iw_sockets_open();
	iw_get_basic_config(skfd,ifname,&cur);
	/* Bring up interface (eg "ip link set IFACE up") */
	struct ifreq req;
	int err;
	strncpy(req.ifr_name,ifname,IFNAMSIZ);
	if ( (err=ioctl(skfd,SIOCGIFFLAGS,&req)) ){
		close(skfd); return 1;
	}
	req.ifr_flags |= IFF_UP;
	if (ioctl(skfd,SIOCSIFFLAGS,&req)) {
		close(skfd); return 1;
	}
	/* Processes command line arguments */
	int i;
	for (i = 1; i < argc; i++) {
		if (strncmp(argv[i],"ad",2)==0) mode |= MODE_ADD;
		else if (strncmp(argv[i],"au",2)==0) mode |= MODE_AUTO;
		else if (strncmp(argv[i],"an",2)==0) mode |= (MODE_ANY & MODE_AUTO);
		else if (strncmp(argv[i],"re",2)==0) mode |= MODE_RECONNECT;
		else if (strncmp(argv[i],"ve",2)==0) mode |= MODE_VERBOSE;
		else fprintf(stderr,"[%s] Ignoring unknown parameter: %s\n",
			argv[0],argv[i]);
	}
	if ( (mode & MODE_VERBOSE) && (mode & MODE_AUTO) ) mode &= ~MODE_VERBOSE;
	/* Scan and select network */
	iw_scan(skfd,ifname,we_ver,&context);
	wireless_scan *ws;
	if (mode & MODE_AUTO) ws = get_best();
	else ws = show_menu();
	if (!ws) {
		iw_sockets_close(skfd);
		fprintf(stderr,"[swifer] no suitable networks found.\n");
		return 1;
	}
	/* Stop any current processes then connect to "ws" */
	system("killall dhcpcd > /dev/null 2>&1 && sleep 0.5");
	system("killall wpa_supplicant > /dev/null 2>&1 && sleep 0.5");
	if ( (mode & MODE_ADD) && is_known(ws) ) mode &= ~MODE_ADD;
	if (ws->b.key_flags == 2048) mode |= MODE_SECURE;
	ws_connect(ws);
	/* Keep alive to reconnect? */
	if (mode & MODE_RECONNECT & MODE_AUTO) {
		while (True /*TODO: connected? */) sleep(TIMEOUT);
		iw_sockets_close(skfd);
		return main(argc,argv);
	}
	else if (mode & MODE_RECONNECT)
		fprintf(stderr,"[%s] reconnect not yet implemented for manual modes.\n",
			argv[0]);
	/* Close up shop */
	iw_sockets_close(skfd);
	return 0;
}

