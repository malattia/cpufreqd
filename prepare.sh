
#!/bin/bash
WANT_AUTOMAKE="1.8" aclocal && libtoolize -c -f && \
aclocal && autoconf && automake -a -c --foreign && \
( \
	echo "Entering src"
	cd src &&
	WANT_AUTOMAKE="1.8" aclocal && \
		autoconf && \
		autoheader && \
		automake -a -c --foreign
	cd ..
) && ( \
	echo "Entering manpages"
	cd manpages &&
	WANT_AUTOMAKE="1.8" aclocal && autoconf && automake -a -c --foreign
	cd ..
) && chmod +x ltmain.sh

		#libtoolize -c -f && \