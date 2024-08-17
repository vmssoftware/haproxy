$ set verify
$
$ haproxy :== $sys$system:haproxy.exe
$ haproxy "-f" "/sys$startup/haproxy.cfg"
$
$ exit
