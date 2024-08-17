$ ! HAPROXY$STARTUP.COM
$ !+
$ ! 24-Mar-2022
$ !-
$
$ set noon
$
$ run/detach -
/input=sys$startup:haproxy$run.com/output=sys$manager:haproxy.log -
/process_name="HAProxy" -
/authorize sys$system:loginout.exe
$
$ exit
