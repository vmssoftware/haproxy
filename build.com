$ @sys$startup:ssl1$startup
$ set verify
$
$ define common [.include.common]
$ define proto [.include.proto]
$ define types [.include.types]
$ define import [.include.import]
$
$ define cc$include [],[.vms],[.ebtree],oss$root:[include]
$ ccopt = "/nolist/names=(as_is,shortened)/first=[.vms]first.h/include=cc$include/warn=disable=(QUESTCOMPARE1,EMPTYINIT,ZEROELEMENTS,BADPTRARITH,EMPTYSTRUCT,QUESTCOMPARE)"
$
$ cc'ccopt'/reent=multi [.src]haproxy.c
$
$ cc'ccopt' [.src]base64.c
$ cc'ccopt' [.src]protocol.c
$ cc'ccopt' [.src]uri_auth.c
$ cc'ccopt' [.src]standard.c
$ cc'ccopt' [.src]buffer.c
$ cc'ccopt' [.src]log.c
$ cc'ccopt' [.src]task.c
$ cc'ccopt' [.src]chunk.c
$ cc'ccopt' [.src]channel.c
$ cc'ccopt' [.src]listener.c
$ cc'ccopt' [.src]lru.c
$ cc'ccopt' [.src]xxhash.c
$ cc'ccopt' [.src]time.c
$ cc'ccopt' [.src]fd.c
$ cc'ccopt' [.src]pipe.c
$ cc'ccopt' [.src]regex.c
$ cc'ccopt' [.src]cfgparse.c
$ cc'ccopt' [.src]server.c
$ cc'ccopt' [.src]checks.c
$ cc'ccopt' [.src]queue.c
$ cc'ccopt' [.src]frontend.c
$ cc'ccopt' [.src]proxy.c
$ cc'ccopt' [.src]peers.c
$ cc'ccopt' [.src]arg.c
$ cc'ccopt' [.src]stick_table.c
$ cc'ccopt' [.src]proto_uxst.c
$ cc'ccopt' [.src]connection.c
$ cc'ccopt' [.src]proto_http.c
$ cc'ccopt' [.src]raw_sock.c
$ cc'ccopt' [.src]backend.c
$ cc'ccopt' [.src]tcp_rules.c
$ cc'ccopt' [.src]lb_chash.c
$ cc'ccopt' [.src]lb_fwlc.c
$ cc'ccopt' [.src]lb_fwrr.c
$ cc'ccopt' [.src]lb_map.c
$ cc'ccopt' [.src]lb_fas.c
$ cc'ccopt' [.src]stream_interface.c
$ cc'ccopt' [.src]stats.c
$ cc'ccopt' [.src]proto_tcp.c
$ cc'ccopt' [.src]applet.c
$ cc'ccopt' [.src]session.c
$ cc'ccopt' [.src]stream.c
$ cc'ccopt' [.src]hdr_idx.c
$ cc'ccopt' [.src]ev_select.c
$ cc'ccopt' [.src]signal.c
$ cc'ccopt' [.src]acl.c
$ cc'ccopt' [.src]sample.c
$ cc'ccopt' [.src]memory.c
$ cc'ccopt' [.src]freq_ctr.c
$ cc'ccopt' [.src]auth.c
$ cc'ccopt' [.src]proto_udp.c
$ cc'ccopt' [.src]compression.c
$ cc'ccopt' [.src]payload.c
$ cc'ccopt' [.src]hash.c
$ cc'ccopt' [.src]pattern.c
$ cc'ccopt' [.src]map.c
$ cc'ccopt' [.src]namespace.c
$ cc'ccopt' [.src]mailers.c
$ cc'ccopt' [.src]dns.c
$ cc'ccopt' [.src]vars.c
$ cc'ccopt' [.src]filters.c
$ cc'ccopt' [.src]flt_http_comp.c
$ cc'ccopt' [.src]flt_trace.c
$ cc'ccopt' [.src]flt_spoe.c
$ cc'ccopt' [.src]cli.c
$ cc'ccopt' [.src]ev_poll.c
$ cc'ccopt' [.ebtree]ebtree.c
$ cc'ccopt' [.ebtree]eb32tree.c
$ cc'ccopt' [.ebtree]eb64tree.c
$ cc'ccopt' [.ebtree]ebmbtree.c
$ cc'ccopt' [.ebtree]ebsttree.c
$ cc'ccopt' [.ebtree]ebimtree.c
$ cc'ccopt' [.ebtree]ebistree.c
$
$ cc'ccopt' [.src]ssl_sock.c
$ cc'ccopt' [.src]shctx.c
$
$ cc'ccopt' [.vms]rlimit.c
$ cc'ccopt' [.vms]scandir.c
$ cc'ccopt' [.vms]syslog.c
$ cc'ccopt' [.vms]vms_init.c
$
$ lib/create libhaproxy.olb
$ purge/log
$
$ lib/insert libhaproxy.olb base64.obj
$ lib/insert libhaproxy.olb protocol.obj
$ lib/insert libhaproxy.olb uri_auth.obj
$ lib/insert libhaproxy.olb standard.obj
$ lib/insert libhaproxy.olb buffer.obj
$ lib/insert libhaproxy.olb log.obj
$ lib/insert libhaproxy.olb task.obj
$ lib/insert libhaproxy.olb chunk.obj
$ lib/insert libhaproxy.olb channel.obj
$ lib/insert libhaproxy.olb listener.obj
$ lib/insert libhaproxy.olb lru.obj
$ lib/insert libhaproxy.olb xxhash.obj
$ lib/insert libhaproxy.olb time.obj
$ lib/insert libhaproxy.olb fd.obj
$ lib/insert libhaproxy.olb pipe.obj
$ lib/insert libhaproxy.olb regex.obj
$ lib/insert libhaproxy.olb cfgparse.obj
$ lib/insert libhaproxy.olb server.obj
$ lib/insert libhaproxy.olb checks.obj
$ lib/insert libhaproxy.olb queue.obj
$ lib/insert libhaproxy.olb frontend.obj
$ lib/insert libhaproxy.olb proxy.obj
$ lib/insert libhaproxy.olb peers.obj
$ lib/insert libhaproxy.olb arg.obj
$ lib/insert libhaproxy.olb stick_table.obj
$ lib/insert libhaproxy.olb proto_uxst.obj
$ lib/insert libhaproxy.olb connection.obj
$ lib/insert libhaproxy.olb proto_http.obj
$ lib/insert libhaproxy.olb raw_sock.obj
$ lib/insert libhaproxy.olb backend.obj
$ lib/insert libhaproxy.olb tcp_rules.obj
$ lib/insert libhaproxy.olb lb_chash.obj
$ lib/insert libhaproxy.olb lb_fwlc.obj
$ lib/insert libhaproxy.olb lb_fwrr.obj
$ lib/insert libhaproxy.olb lb_map.obj
$ lib/insert libhaproxy.olb lb_fas.obj
$ lib/insert libhaproxy.olb stream_interface.obj
$ lib/insert libhaproxy.olb stats.obj
$ lib/insert libhaproxy.olb proto_tcp.obj
$ lib/insert libhaproxy.olb applet.obj
$ lib/insert libhaproxy.olb session.obj
$ lib/insert libhaproxy.olb stream.obj
$ lib/insert libhaproxy.olb hdr_idx.obj
$ lib/insert libhaproxy.olb ev_select.obj
$ lib/insert libhaproxy.olb signal.obj
$ lib/insert libhaproxy.olb acl.obj
$ lib/insert libhaproxy.olb sample.obj
$ lib/insert libhaproxy.olb memory.obj
$ lib/insert libhaproxy.olb freq_ctr.obj
$ lib/insert libhaproxy.olb auth.obj
$ lib/insert libhaproxy.olb proto_udp.obj
$ lib/insert libhaproxy.olb compression.obj
$ lib/insert libhaproxy.olb payload.obj
$ lib/insert libhaproxy.olb hash.obj
$ lib/insert libhaproxy.olb pattern.obj
$ lib/insert libhaproxy.olb map.obj
$ lib/insert libhaproxy.olb namespace.obj
$ lib/insert libhaproxy.olb mailers.obj
$ lib/insert libhaproxy.olb dns.obj
$ lib/insert libhaproxy.olb vars.obj
$ lib/insert libhaproxy.olb filters.obj
$ lib/insert libhaproxy.olb flt_http_comp.obj
$ lib/insert libhaproxy.olb flt_trace.obj
$ lib/insert libhaproxy.olb flt_spoe.obj
$ lib/insert libhaproxy.olb cli.obj
$ lib/insert libhaproxy.olb ev_poll.obj
$ lib/insert libhaproxy.olb ebtree.obj
$ lib/insert libhaproxy.olb eb32tree.obj
$ lib/insert libhaproxy.olb eb64tree.obj
$ lib/insert libhaproxy.olb ebmbtree.obj
$ lib/insert libhaproxy.olb ebsttree.obj
$ lib/insert libhaproxy.olb ebimtree.obj
$ lib/insert libhaproxy.olb ebistree.obj
$
$ lib/insert libhaproxy.olb ssl_sock.obj
$ lib/insert libhaproxy.olb shctx.obj
$
$ lib/insert libhaproxy.olb rlimit.obj
$ lib/insert libhaproxy.olb scandir.obj
$ lib/insert libhaproxy.olb syslog.obj
$ lib/insert libhaproxy.olb vms_init.obj
$
$ link/threads/exe=haproxy.exe haproxy.obj,sys$input/opt
libhaproxy.olb/lib
oss$root:[lib]libregex.olb/lib
ssl1$root:[lib]ssl1$libssl32.olb/lib
ssl1$root:[lib]ssl1$libcrypto32.olb/lib
$
$ purge/log
$ exit
