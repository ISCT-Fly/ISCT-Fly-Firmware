# PlatformIO passes build_flags to the compiler only.  The linker needs the FPU options too,
# otherwise it picks the soft-float C library and refuses to link the hard-float objects.
Import("env")  # noqa: F821  (provided by PlatformIO)

env.Append(LINKFLAGS=["-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"])  # noqa: F821
