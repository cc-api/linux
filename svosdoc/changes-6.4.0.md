These notes aren't official yet so we'll allow a loose formatting standard until 6.4.0
is due to release.

# libtraceevent

We saw this pile of stuff, of which the missing libtraceevent was fatal:

```
Makefile.config:458: No libdw DWARF unwind found, Please install elfutils-devel/libdw-dev >= 0.158 and/or set LIBDW_DIR
Makefile.config:463: No libdw.h found or old libdw.h found or elfutils is older than 0.138, disables dwarf support. Please install new elfutils-devel/libdw-dev
Makefile.config:588: DWARF support is off, BPF prologue is disabled
Makefile.config:596: No sys/sdt.h found, no SDT events are defined, please install systemtap-sdt-devel or systemtap-sdt-dev
Makefile.config:763: slang not found, disables TUI support. Please install slang-devel, libslang-dev or libslang2-dev
Makefile.config:810: Missing perl devel files. Disabling perl scripting support, please install perl-ExtUtils-Embed/libperl-dev
Makefile.config:968: No libzstd found, disables trace compression, please install libzstd-dev[el] and/or set LIBZSTD_DIR
Makefile.config:979: No libcap found, disables capability support, please install libcap-devel/libcap-dev
Makefile.config:992: No numa.h found, disables 'perf bench numa mem' benchmark, please install numactl-devel/libnuma-devel/libnuma-dev
Makefile.config:1051: No libbabeltrace found, disables 'perf data' CTF format support, please install libbabeltrace-dev[el]/libbabeltrace-ctf-dev
Makefile.config:1080: No alternatives command found, you need to set JDIR= to point to the root of your Java directory
Makefile.config:1142: libpfm4 not found, disables libpfm4 support. Please install libpfm4-dev
Makefile.config:1160: *** ERROR: libtraceevent is missing. Please install libtraceevent-dev/libtraceevent-devel or build with NO_LIBTRACEEVENT=1.  Stop.
```

Searching, we saw this wrt 6.4.0:
https://lore.kernel.org/lkml/ZFTOB9ZXsKuxXm%2F6@krava/T/

I tried to add libtraceevent-dev first and rebuilding. It still puked, but in a new way:

```
  GEN     python/perf.cpython-39-x86_64-linux-gnu.so
In file included from /home/acpreble/src/svos_next/build-6.4.0-x86-64-svos-next-default/tools/perf/util/evsel.c:43:
/home/acpreble/src/svos_next/build-6.4.0-x86-64-svos-next-default/tools/perf/util/trace-event.h:143:62: error: operator '&&' has no right operand
  143 | #if defined(LIBTRACEEVENT_VERSION) &&  LIBTRACEEVENT_VERSION >= MAKE_LIBTRACEEVENT_VERSION(1, 5, 0)
      |                                                              ^~
error: command '/usr/bin/gcc' failed with exit code 1
```

Trying with NO_LIBTRACEEVENT=1 cleared it. So for now, we have added `NO_LIBTRACEEVENT=1` to all the $(MAKE) commands in debian/template.mk. It wasn't ever made more clear where it needed to be set and KCFLAGS was not it.
