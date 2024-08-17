$ ! HAPROXY$SHUTDOWN.COM
$ !+
$ ! 24-Mar-2022
$ !-
$
$ set noon
$ write sys$output "%HAPROXY-I-SHUTDOWN, shutting down HAProxy process"
$
$ ctx = ""
$ tmp = f$context("process",ctx,"prcnam","HAPROXY","eql")
$ pid = f$pid(ctx)
$
$ if (pid .eqs. "")
$ then
$    write sys$output "%HAPROXY-W-NOTRUN, HAProxy might not be running"
$ else
$    stop proc/id='pid'
$ endif
$
$ exit
