/****************************************************************************\
* WIFI - wifi connector with automatic connection modes
* By: Jesse McClure, copyright 2012
* License: GPLv3
* USAGE: wifi [ auto | any | add | reconnect ]
\****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ncurses.h>
#include <iwlib.h>
#include <netinet/in.h>

/* for wireless tools v30.pre-9 */
#ifndef PROC_NET_WIRELESS
#define PROC_NET_WIRELESS "/proc/net/wireless"
#endif /* PROC_NET_WIRELESS */

#define MAX(a,b)	(a > b ? a : b)

#define True	1
#define False	0

#define TIMEOUT		10
#define THRESHOLD	10
#define MAX_LINE	255
#define DHCPLEN		24

#define MODE_AUTO		0x0001
#define MODE_ANY		0x0002
#define MODE_ADD		0x0010
#define MODE_RECONNECT	0x0100
#define MODE_SECURE		0x1000
#define MODE_VERBOSE	0x2000
#define MODE_HIDDEN		0x4000

static int draw_entry(wireless_scan *, int);
static int is_known(wireless_scan *);
static wireless_scan *get_best();
static int refresh_list();
static int remove_network(const char *);
static wireless_scan *show_menu();
static int spawn(const char *,const char *);
static int ws_connect(wireless_scan *);

static char ifname[IFNAMSIZ+1] = "wlan0";
static const char *config = "/etc/swifer.conf";
static const char *netpath = "/usr/share/swifer/";
static char *hook_preup = NULL, *hook_postup = NULL;
static int we_ver, skfd, mode;
static wireless_scan_head context;
static wireless_config cur;
static char cmd[MAX_LINE+1] = "";
static char *killall = "killall", *wpa_sup = "wpa_supplicant", *re = "re", *an = "an";
static char dhcp[DHCPLEN+1] = "dhcpcd";
static const char *noname = "    <hidden>";

int draw_entry(wireless_scan *ws,int sel) {
	char *name = ws->b.essid;
	if (!strlen(name)) name = (char *) noname;
	/* known and/or currently connected */
	if (strncmp(cur.essid,name,IW_ESSID_MAX_SIZE)==0) attrset(COLOR_PAIR(3)|A_BOLD);
	else if (ws->b.key_flags == 2048) attrset(COLOR_PAIR(1)|A_BOLD);
	else attrset(COLOR_PAIR(2)|A_BOLD);
	if (is_known(ws)) printw("=> ");
	else printw(" > ");
	/* ESSID & selection cursor */
	if (sel) attrset(COLOR_PAIR(3)|A_BOLD|A_REVERSE);
	else attrset(COLOR_PAIR(0));
	printw(" %-*s ",IW_ESSID_MAX_SIZE+2,name);
	/* Connection strength */
	int perc = 100 * ws->stats.qual.qual / 70;
	if (perc > 94) attrset(COLOR_PAIR(2)|A_BOLD);
	else if (perc > 84) attrset(COLOR_PAIR(2));
	else if (perc > 64) attrset(COLOR_PAIR(3)|A_BOLD);
	else attrset(COLOR_PAIR(1)|A_BOLD);
	printw("%3d%%\n",perc);
	return 1;
}

int is_known(wireless_scan *ws) {
	if (!strlen(ws->b.essid)) return False;
	FILE *cfg = fopen(config,"r");
	if (!cfg) return False;
	char line[MAX_LINE+1];
	while ( (fgets(line,MAX_LINE,cfg)) != NULL)
		if (strncmp(line,"[NETWORKS]",10) == 0)
			break;
	while ( (fgets(line,MAX_LINE,cfg)) != NULL)	{
		line[strlen(line)-1] = '\0';
		if (strcmp(line,ws->b.essid) == 0) {
			fclose(cfg);
			return True;
		}
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
	attrset(COLOR_PAIR(3)|A_BOLD);
	printw("\n\n  Getting wifi listing ...\n");
	refresh();
	iw_scan(skfd,ifname,we_ver,&context);
	wireless_scan *ws;
	int n;
	for (ws = context.result, n=0; ws; ws = ws->next)
		if ((mode & MODE_HIDDEN) || strlen(ws->b.essid)) n++;
	clear();
	return n-1;
}

int remove_network(const char *network) {
	sprintf(cmd,"rm \"%s%s\" > /dev/null 2>&1",netpath,network);
	system(cmd);
	FILE *cfg;
	FILE *tmp;
	if ( !(cfg=fopen(config,"r")) ) exit(1);
	if ( !(tmp=fopen("/tmp/swifer.tmp","w")) ) {
		fclose(cfg); exit(1);
	}
	while ( fgets(cmd,MAX_LINE,cfg) != NULL ) {
		if (strncmp(cmd,network,strlen(network)) != 0) {
			fprintf(tmp,cmd);
		}
	}
	fclose(cfg); fclose(tmp);
	sprintf(cmd,"mv /tmp/swifer.tmp %s",config);
	system(cmd);
	exit(0);
}

wireless_scan *show_menu() {
	/* Init ncurses */
	initscr(); raw(); noecho(); curs_set(0);
	start_color(); use_default_colors();
	init_pair(1,1,-1); init_pair(2,2,-1); init_pair(3,3,-1);
	init_pair(4,4,-1); init_pair(5,5,-1); init_pair(6,6,-1);
	init_pair(7,7,-1);
	/* Select entry */
	wireless_scan *ws, *ss;
	int running = True;
	int c,i,sel = 0;
	int n = refresh_list();
	while (running) {
		move(0,0);
		attrset(COLOR_PAIR(4)|A_REVERSE|A_BOLD);
		printw(" *  %-*s  %%  \n",IW_ESSID_MAX_SIZE+2,"Network"); 
		for (ws = context.result, i=0; ws; ws = ws->next)
			if ((mode & MODE_HIDDEN) || strlen(ws->b.essid)) {
				if (i == sel) ss = ws;
				draw_entry(ws,((i++)==sel));
			}
		attrset(COLOR_PAIR(4)|A_REVERSE|A_BOLD);
		printw("  %-*s \n",IW_ESSID_MAX_SIZE+8," "); 
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
	endwin();
	if (!running) {	/* "q" selected */
		iw_sockets_close(skfd);
		exit(0);
	}
	for (i = 0, ws = context.result; i != sel; ws = ws->next)
		if ((mode & MODE_HIDDEN) || strlen(ws->b.essid)) i++;
	/* End ncurses session & return result */
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
	if (hook_preup) system(hook_preup);
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
		sprintf(cmd,"wpa_passphrase \"%s\" \"%s\" > \"%s\"",ws->b.essid,psk,netfile);
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
		sprintf(cmd,"rm \"%s\"",netfile);
		system(cmd);
	}	
	if (netfile) {
		free(netfile);
		netfile = NULL;
	}
	spawn(dhcp,netfile);
	if (hook_postup) system(hook_postup);
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
			if (line[0] == '#') continue;
			if (sscanf(line,"INTERFACE = %s",val))
				strncpy(ifname,val,IFNAMSIZ);
			else if (sscanf(line,"DHCP = %s",val))
				strncpy(dhcp,val,DHCPLEN);
			else if (sscanf(line,"PRE_UP = %s",val))
				hook_preup = strdup(val);
			else if (sscanf(line,"POST_UP = %s",val))
				hook_postup = strdup(val);
			else if (strncmp(line,"[NETWORKS]",10)==0)
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
		int loop = 0;
		while ( (err=ioctl(skfd,SIOCGIFFLAGS,&req)) ) {
			usleep(100000);
			if (loop++ > 50) break;
		}
		if (err) {
			close(skfd);
			return 2;
		}
	}
	req.ifr_flags |= IFF_UP;
	if (ioctl(skfd,SIOCSIFFLAGS,&req)) {
		close(skfd); return 3;
	}
	/* Processes command line arguments */
	int i;
	for (i = 1; i < argc; i++) {
		if (strncmp(argv[i],"ad",2)==0) mode |= MODE_ADD;
		else if (strncmp(argv[i],"au",2)==0) mode |= MODE_AUTO;
		else if (strncmp(argv[i],"hi",2)==0) mode |= MODE_HIDDEN;
		else if (strncmp(argv[i],"an",2)==0) mode |= (MODE_ANY | MODE_AUTO);
		else if (strncmp(argv[i],"re",2)==0) mode |= (MODE_RECONNECT | MODE_AUTO);
		else if (strncmp(argv[i],"ve",2)==0) mode |= MODE_VERBOSE;
		else if (strncmp(argv[i],"de",2)==0) {
			if (argc > i+1) remove_network(argv[i+1]);
		}
		else fprintf(stderr,"[%s] Ignoring unknown parameter: %s\n",
			argv[0],argv[i]);
	}
	if ( (mode & MODE_VERBOSE) && (mode & MODE_AUTO) ) mode &= ~MODE_VERBOSE;
	/* Scan and select network */
	iw_scan(skfd,ifname,we_ver,&context);
	wireless_scan *ws;
	if (mode & MODE_AUTO) ws = get_best();
	else ws = show_menu();
	const char *arg[4];
	if (ws) { /* Stop any current processes then connect to "ws" */
		arg[0] = killall; arg[1] = dhcp; arg[2] = NULL;
		if (fork()==0) {
			fclose(stdout); fclose(stderr);
			execvp(arg[0],(char * const *) arg);
		}
		arg[1] = wpa_sup;
		if (fork()==0) {
			fclose(stdout); fclose(stderr);
			execvp(arg[0],(char * const *) arg);
		}
		sleep(1);
		if ( (mode & MODE_ADD) && is_known(ws) ) mode &= ~MODE_ADD;
		if (ws->b.key_flags == 2048) mode |= MODE_SECURE;
		mode_t pre = umask(S_IWGRP|S_IWOTH|S_IRGRP|S_IROTH);
		ws_connect(ws);
		umask(pre);
	}
	else if ( !(mode & MODE_RECONNECT) ) {
		fprintf(stderr,"[swifer] no suitable networks found.\n");
		return 5;
	}
	/* Keep alive to reconnect? */
	iw_sockets_close(skfd);
	if (mode & MODE_RECONNECT) {
		if (fork() == 0) {
			setsid();
			int level = THRESHOLD + 1, ret;
			char scanline[256];
			snprintf(scanline,255,"%%*[^\n]\n%%*[^\n]\n%s: %%*d %%d.",ifname);
			FILE *procw;
			while (level > THRESHOLD) {
				sleep(TIMEOUT);
				procw = fopen(PROC_NET_WIRELESS,"r");
				ret = fscanf(procw,scanline,&level);
				fclose(procw);
				if (ret != 1) level = 0;
			}
			arg[0] = argv[0]; arg[1] = re; arg[2] = an; arg[3] = NULL;
			if ( !(mode & MODE_ANY)) arg[2] = NULL;
			execvp(arg[0],(char * const *) arg);
		}
	}
	if (hook_preup) free(hook_preup);
	if (hook_postup) free(hook_postup);
	return 0;
}

