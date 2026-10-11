Import("env", "projenv")
# ch32fun enables LTO internally. Keep benchmark translation units separate.
env.ProcessUnFlags("-flto")
env.Append(CCFLAGS=["-fno-lto"], LINKFLAGS=["-fno-lto"])
projenv.ProcessUnFlags("-flto")
projenv.Append(CCFLAGS=["-fno-lto"])
