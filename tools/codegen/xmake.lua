rule("codegen")
    set_extensions(".proto")

    on_load(function(target)
        target:add("deps", "NetPack")
    end)

    on_config(function(target)
        local sourcebatch = target:sourcebatches()["codegen"]
    
        for _, sourcefile in ipairs(sourcebatch.sourcefiles) do
            local outputSourceFile = path.join(target:autogendir(), "rules", "netpack", path.basename(sourcefile) .. ".gen.cpp")
            local objectfile = target:objectfile(outputSourceFile)
            table.insert(target:objectfiles(), objectfile)
        end

        target:add("includedirs", path.join(target:autogendir(), "rules", "netpack"), {public = true})
    end)

    -- Generation happens here, for every proto, BEFORE any of the per-file compile
    -- batches run. It used to happen inside each file's batch, right before compiling
    -- that file's .gen.cpp - but the generated headers include each other across protos
    -- (server.gen.h includes common.gen.h), so with parallel build jobs one proto's
    -- .gen.cpp could compile before a sibling proto had generated the header it
    -- includes. Machines with generated files left over from earlier builds never see
    -- it; a fresh checkout rolls dice. Serial and mtime-guarded, so an unchanged proto
    -- costs nothing and unchanged outputs keep their timestamps.
    before_build(function (target)
        local netpack = target:dep("NetPack"):targetfile()
        local output_dir = path.join(target:autogendir(), "rules", "netpack")
        local sourcebatch = target:sourcebatches()["codegen"]
        if not sourcebatch then return end

        -- Unconditionally, every build. An mtime guard lived here briefly and created the
        -- worst bug of the project's life: switching branches can leave a generated file
        -- NEWER than its .proto, so generation silently skips and the build compiles the
        -- OTHER branch's wire format - a client that frames one bit differently than its
        -- own protocol identifier claims, shipping corruption that no identifier check
        -- can catch. Three proto files regenerate in milliseconds; correctness is free.
        for _, sourcefile in ipairs(sourcebatch.sourcefiles) do
            os.vrunv(netpack, {sourcefile, output_dir})
        end
    end)

    before_buildcmd_file(function(target, batchcmds, sourcefile, opt)
        import("core.project.project")
		import("core.tool.toolchain")

        -- add commands
		batchcmds:show_progress(opt.progress, "${color.build.object}compiling.netpack %s", sourcefile)

        local output_dir = path.join(target:autogendir(), "rules", "netpack")
        target:add("includedirs", output_dir, {public = true})

		local outputHeaderFile = path.join(target:autogendir(), "rules", "netpack", path.basename(sourcefile) .. ".gen.h")
        local outputSourceFile = path.join(target:autogendir(), "rules", "netpack", path.basename(sourcefile) .. ".gen.cpp")

        local objectfile = target:objectfile(outputSourceFile)
        batchcmds:compile(outputSourceFile, objectfile)

		-- The staleness question this batch must answer is "is the OBJECT older than the
		-- GENERATED SOURCES" - nothing else. It used to compare the proto against the
		-- header, which inverted the logic once generation moved to before_build: the
		-- header is re-stamped before this check ever runs, so the batch always judged
		-- itself up to date and a regenerated .gen.cpp was NEVER recompiled. That linked
		-- one branch's serializer objects under another branch's headers - a client whose
		-- wire framing disagreed with its own protocol identifier by one bit, doubling
		-- every entity id it sent. Depfiles are now the generated files themselves (plus
		-- the proto), measured against the object.
		batchcmds:add_depfiles(sourcefile, outputSourceFile, outputHeaderFile)
		batchcmds:set_depmtime(os.mtime(objectfile))
		batchcmds:set_depcache(target:dependfile(objectfile))
    end)