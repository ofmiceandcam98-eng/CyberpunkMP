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

        for _, sourcefile in ipairs(sourcebatch.sourcefiles) do
            local header = path.join(output_dir, path.basename(sourcefile) .. ".gen.h")
            if not os.isfile(header) or os.mtime(sourcefile) > os.mtime(header) then
                os.vrunv(netpack, {sourcefile, output_dir})
            end
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
        --table.insert(target:objectfiles(), objectfile)
        batchcmds:compile(outputSourceFile, objectfile)

		-- add deps
		batchcmds:add_depfiles(sourcefile)
		batchcmds:set_depmtime(os.mtime(outputHeaderFile))
		batchcmds:set_depcache(target:dependfile(outputHeaderFile))
        batchcmds:set_depcache(target:dependfile(outputSourceFile))
    end)