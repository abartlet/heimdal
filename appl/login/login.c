/*
 * Copyright (c) 1997, 1998, 1999 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 *
 * 3. All advertising materials mentioning features or use of this software 
 *    must display the following acknowledgement: 
 *      This product includes software developed by Kungliga Tekniska 
 *      H�gskolan and its contributors. 
 *
 * 4. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

#include "login_locl.h"

RCSID("$Id$");

/*
 * the environment we will send to execle and the shell.
 */

static char **env;
static int num_env;

static void
extend_env(char *str)
{
    env = realloc(env, (num_env + 1) * sizeof(*env));
    if(env == NULL)
	errx(1, "Out of memory!");
    env[num_env++] = str;
}

static void
add_env(const char *var, const char *value)
{
    int i;
    char *str;
    asprintf(&str, "%s=%s", var, value);
    if(str == NULL)
	errx(1, "Out of memory!");
    for(i = 0; i < num_env; i++)
	if(strncmp(env[i], var, strlen(var)) == 0 && 
	   env[i][strlen(var)] == '='){
	    free(env[i]);
	    env[i] = str;
	    return;
	}
    
    extend_env(str);
}

static void
copy_env(void)
{
    char **p;
    for(p = environ; *p; p++)
	extend_env(*p);
}

static void
exec_shell(const char *shell, int fallback)
{
    char *sh;
    const char *p;
    p = strrchr(shell, '/');
    if(p)
	p++;
    else
	p = shell;
    asprintf(&sh, "-%s", p);
    extend_env(NULL);
    execle(shell, sh, NULL, env);
    if(fallback){
	warnx("Can't exec %s, trying %s", 
	      shell, _PATH_BSHELL);
	execle(_PATH_BSHELL, "-sh", NULL, env);
	err(1, "%s", _PATH_BSHELL);
    }
    err(1, "%s", shell);
}

static enum { AUTH_KRB4, AUTH_KRB5 } auth;

#ifdef KRB5
static krb5_context context;
static krb5_ccache  id, id2;

static int
krb5_verify(struct passwd *pwd, const char *password)
{
    krb5_error_code ret;
    krb5_principal princ;

    ret = krb5_init_context(&context);
    if(ret)
	return 1;
	    
    ret = krb5_parse_name(context, pwd->pw_name, &princ);
    if(ret){
	krb5_free_context(context);
	return 1;
    }
    ret = krb5_cc_gen_new(context, &krb5_mcc_ops, &id);
    if(ret){
	krb5_free_principal(context, princ);
	krb5_free_context(context);
	return 1;
    }
    ret = krb5_verify_user(context,
			   princ, 
			   id,
			   password, 
			   1,
			   NULL);
    krb5_free_principal(context, princ);
    if (ret)
	krb5_free_context (context);
    return ret;
}

static int
krb5_start_session (struct passwd *pwd)
{
    krb5_error_code ret;
    char residual[32];

    /* copy credentials to file cache */
    snprintf(residual, sizeof(residual), "FILE:/tmp/krb5cc_%u", 
	     (unsigned)pwd->pw_uid);
    krb5_cc_resolve(context, residual, &id2);
    ret = krb5_cc_copy_cache(context, id, id2);
    krb5_cc_close(context, id2);
    if (ret == 0)
	add_env("KRB5CCNAME", residual);
    else
	krb5_cc_destroy (context, id2);
    krb5_cc_destroy(context, id);
    return ret;
}

static void
krb5_finish (void)
{
    krb5_free_context(context);
}

#ifdef KRB4

static void
krb5_get_afs_tokens (struct passwd *pwd)
{
    char cell[64];
    char *pw_dir;

    if (!k_hasafs ())
	return;

#ifdef _AIX
    /* XXX this is a fix for a bug in AFS for AIX 4.3, w/o
	   this hack the kernel crashes on the following
	   pioctl... */
    pw_dir = strdup(pwd->pw_dir);
#else
    pw_dir = pwd->pw_dir;
#endif

    k_setpag();

    if(k_afs_cell_of_file(pw_dir, cell, sizeof(cell)) == 0)
	krb5_afslog_uid_home (context, id2,
			      cell, NULL, pwd->pw_uid, pwd->pw_dir);
    krb5_afslog_uid_home (context, id2, NULL, NULL,
			  pwd->pw_uid, pwd->pw_dir);
}

#endif /* KRB4 */

#endif /* KRB5 */

#ifdef KRB4

static int
krb4_verify(struct passwd *pwd, const char *password)
{
    char lrealm[REALM_SZ];
    int ret;
    char ticket_file[MaxPathLen];

    ret = krb_get_lrealm (lrealm, 1);
    if (ret)
	return 1;

    snprintf (ticket_file, sizeof(ticket_file),
	      "%s%u_%u",
	      TKT_ROOT, (unsigned)pwd->pw_uid, (unsigned)getpid());

    krb_set_tkt_string (ticket_file);

    ret = krb_verify_user (pwd->pw_name, "", lrealm, (char *)password,
			   KRB_VERIFY_SECURE_FAIL, NULL);
    if (ret)
	return 1;

    if (chown (ticket_file, pwd->pw_uid, pwd->pw_gid) < 0) {
	dest_tkt();
	return 1;
    }
	
    add_env ("KRBTKFILE", ticket_file);
    return 0;
}

static void
krb4_get_afs_tokens (struct passwd *pwd)
{
    char cell[64];
    char *pw_dir;

    if (!k_hasafs ())
	return;

#ifdef _AIX
    /* XXX this is a fix for a bug in AFS for AIX 4.3, w/o
	   this hack the kernel crashes on the following
	   pioctl... */
    pw_dir = strdup(pwd->pw_dir);
#else
    pw_dir = pwd->pw_dir;
#endif

    k_setpag();

    if(k_afs_cell_of_file(pw_dir, cell, sizeof(cell)) == 0)
	krb_afslog_uid_home (cell, NULL, pwd->pw_uid, pwd->pw_dir);

    krb_afslog_uid_home (NULL, NULL, pwd->pw_uid, pwd->pw_dir);
}

#endif /* KRB4 */

static int f_flag;
static int p_flag;
static int r_flag;
static int version_flag;
static int help_flag;
static char *remote_host;

struct getargs args[] = {
#if 0
    { NULL, 'a' },
    { NULL, 'd' },
#endif
    { NULL, 'f', arg_flag,	&f_flag,	"pre-authenticated" },
    { NULL, 'h', arg_string,	&remote_host,	"remote host", "hostname" },
    { NULL, 'p', arg_flag,	&p_flag,	"don't purge environment" },
#if 0
    { NULL, 'r', arg_flag,	&r_flag,	"rlogin protocol" },
#endif
    { "version", 0,  arg_flag,	&version_flag },
    { "help",	 0,  arg_flag,&help_flag, }
};

int nargs = sizeof(args) / sizeof(args[0]);

static void
update_utmp(const char *username, const char *hostname,
	    char *tty, char *ttyn)
{
    /*
     * Update the utmp files, both BSD and SYSV style.
     */
    if (utmpx_login(tty, username, hostname) != 0 && !f_flag) {
	printf("No utmpx entry.  You must exec \"login\" from the "
	       "lowest level shell.\n");
	exit(1);
    }
    utmp_login(ttyn, username, hostname);
}

static void
checknologin(void)
{
    FILE *f;
    char buf[1024];

    f = fopen(_PATH_NOLOGIN, "r");
    if(f == NULL)
	return;
    while(fgets(buf, sizeof(buf), f))
	fputs(buf, stdout);
    fclose(f);
    exit(0);
}


static void
do_login(struct passwd *pwd, char *tty, char *ttyn)
{
    int rootlogin = (pwd->pw_uid == 0);

    if(pwd->pw_uid != 0)
	checknologin();
    
    update_utmp(pwd->pw_name, remote_host ? remote_host : "",
		tty, ttyn);
#ifdef HAVE_SETLOGIN
    if(setlogin(pwd->pw_name)){
	warn("setlogin(%s)", pwd->pw_name);
	if(rootlogin == 0)
	    exit(1);
    }
#endif
#ifdef HAVE_INITGROUPS
    if(initgroups(pwd->pw_name, pwd->pw_gid)){
	warn("initgroups(%s, %u)", pwd->pw_name, (unsigned)pwd->pw_gid);
	if(rootlogin == 0)
	    exit(1);
    }
#endif
    if(setgid(pwd->pw_gid)){
	warn("setgid(%u)", (unsigned)pwd->pw_gid);
	if(rootlogin == 0)
	    exit(1);
    }
    if(setuid(pwd->pw_uid)){
	warn("setuid(%u)", (unsigned)pwd->pw_uid);
	if(rootlogin == 0)
	    exit(1);
    }
    /* perhaps work some magic */
    if(do_osfc2_magic(pwd->pw_uid))
	exit(1);
#if defined(HAVE_GETUDBNAM) && defined(HAVE_SETLIM)
    {
	struct udb *udb;
	long t;
	const long maxcpu = 46116860184; /* some random constant */
	udb = getudbnam(pwd->pw_name);
	if(udb == UDB_NULL)
	    errx(1, "Failed to get UDB entry.");
	t = udb->ue_pcpulim[UDBRC_INTER];
	if(t == 0 || t > maxcpu)
	    t = CPUUNLIM;
	else
	    t *= 100 * CLOCKS_PER_SEC;

	if(limit(C_PROC, 0, L_CPU, t) < 0)
	    warn("limit C_PROC");

	t = udb->ue_jcpulim[UDBRC_INTER];
	if(t == 0 || t > maxcpu)
	    t = CPUUNLIM;
	else
	    t *= 100 * CLOCKS_PER_SEC;

	if(limit(C_JOBPROCS, 0, L_CPU, t) < 0)
	    warn("limit C_JOBPROCS");

	nice(udb->ue_nice[UDBRC_INTER]);
    }
#endif
    if (chdir(pwd->pw_dir) < 0) {
	fprintf(stderr, "No home directory \"%s\"!\n", pwd->pw_dir);
	if (chdir("/"))
	    exit(0);
	pwd->pw_dir = "/";
	fprintf(stderr, "Logging in with home = \"/\".\n");
    }
#ifdef KRB5
    if (auth == AUTH_KRB5) {
	krb5_start_session (pwd);
#ifdef KRB4
	krb5_get_afs_tokens (pwd);
#endif /* KRB4 */
	krb5_finish ();
    }
#endif /* KRB5 */

#ifdef KRB4
    if (auth == AUTH_KRB4) {
	krb4_get_afs_tokens (pwd);
    }
#endif /* KRB4 */

    add_env("HOME", pwd->pw_dir);
    add_env("USER", pwd->pw_name);
    add_env("LOGNAME", pwd->pw_name);
    add_env("SHELL", pwd->pw_shell);
    exec_shell(pwd->pw_shell, rootlogin);
}

static int
check_password(struct passwd *pwd, const char *password)
{
    if(pwd->pw_passwd == NULL)
	return 1;
    if(pwd->pw_passwd[0] == '\0'){
#ifdef ALLOW_NULL_PASSWORD
	return password[0] != '\0';
#else
	return 1;
#endif
    }
    if(strcmp(pwd->pw_passwd, crypt(password, pwd->pw_passwd)) == 0)
	return 0;
#ifdef KRB5
    if(krb5_verify(pwd, password) == 0) {
	auth = AUTH_KRB5;
	return 0;
    }
#endif
#ifdef KRB4
    if (krb4_verify (pwd, password) == 0) {
	auth = AUTH_KRB4;
	return 0;
    }
#endif
    return 1;
}

static void
usage(int status)
{
    arg_printusage(args, nargs, NULL, "[username]");
    exit(status);
}

int
main(int argc, char **argv)
{
    int max_tries = 5;
    int try;

    char username[32];
    int optind = 0;

    int ask = 1;
    
    set_progname(argv[0]);

    openlog("login", LOG_ODELAY, LOG_AUTH);

    if (getarg (args, sizeof(args) / sizeof(args[0]), argc, argv,
		&optind))
	usage (1);
    argc -= optind;
    argv += optind;

    if(help_flag)
	usage(0);
    if (version_flag) {
	print_version (NULL);
	return 0;
    }
	
    if (geteuid() != 0)
	err(1, "only root may use login, use su");

    /* Default tty settings. */
    stty_default();

    if(p_flag)
	copy_env();

    if(*argv){
	if(strchr(*argv, '=') == NULL && strcmp(*argv, "-") != 0){
	    strcpy_truncate (username, *argv, sizeof(username));
	    ask = 0;
	}
    }
    for(try = 0; try < max_tries; try++){
	struct passwd *pwd;
	char password[128];
	int ret;
	char ttname[32];
	char *tty, *ttyn;

	if(ask){
	    f_flag = r_flag = 0;
	    ret = read_string("login: ", username, sizeof(username), 1);
	    if(ret == -3)
		exit(0);
	    if(ret == -2)
		continue;
	}
	if(f_flag == 0){
	    ret = read_string("Password: ", password, sizeof(password), 0);
	    if(ret == -3 || ret == -2)
		continue;
	}
	pwd = getpwnam(username);
	if(pwd == NULL){
	    fprintf(stderr, "Login incorrect.\n");
	    ask = 1;
	    continue;
	}
	
	if(f_flag == 0 && check_password(pwd, password)){
	    fprintf(stderr, "Login incorrect.\n");
	    continue;
	}
	ttyn = ttyname(STDIN_FILENO);
	if(ttyn == NULL){
	    snprintf(ttname, sizeof(ttname), "%s??", _PATH_TTY);
	    ttyn = ttname;
	}
	if((tty = strrchr(ttyn, '/')))
	    tty++;
	else
	    tty = ttyn;
    
	if (login_access (pwd, remote_host ? remote_host : tty) == 0) {
	    fprintf(stderr, "Permission denied\n");
	    if (remote_host)
		syslog(LOG_NOTICE, "%s LOGIN REFUSED FROM %s",
		       pwd->pw_name, remote_host);
	    else
		syslog(LOG_NOTICE, "%s LOGIN REFUSED ON %s",
		       pwd->pw_name, tty);
	    exit (1);
	}
	do_login(pwd, tty, ttyn);
    }
    exit(1);
}
