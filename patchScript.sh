for patchfile in extra-patches/*.patch; do
patch -N -p0 < $patchfile
done
