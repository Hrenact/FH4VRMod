// Targeted offline analysis of a read-only FH4 runtime image export.
// @category FH4VR.Research
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

public class DecompileUi extends GhidraScript {
    private Address base;
    private AddressSet code;
    private final Map<Long, String> names = new LinkedHashMap<>();
    private final Set<Long> visited = new LinkedHashSet<>();
    private final JsonArray results = new JsonArray();
    private Path output;

    private Address at(long rva) { return base.add(rva); }
    private long rva(Address addr) { return addr.subtract(base); }
    private String hex(long value) { return "0x" + Long.toHexString(value); }

    private void saveIndex(JsonObject manifest, ArrayDeque<long[]> queue) throws IOException {
        JsonObject index = new JsonObject();
        index.addProperty("runtime_base", hex(base.getOffset()));
        index.addProperty("raw_sha256", manifest.get("sha256").getAsString());
        index.addProperty("pending_functions", queue.stream().map(item -> item[0]).filter(value -> !visited.contains(value)).distinct().count());
        index.add("functions", results);
        Files.writeString(output.resolve("index.json"), new GsonBuilder().setPrettyPrinting().create().toJson(index), StandardCharsets.UTF_8);
    }

    private List<Long> exportOne(long entryRva, int depth, DecompInterface decompiler) throws Exception {
        List<Long> callees = new ArrayList<>();
        Address entry = at(entryRva);
        JsonObject row = new JsonObject();
        row.addProperty("rva", hex(entryRva));
        row.addProperty("depth", depth);
        results.add(row);
        if (!code.contains(entry)) {
            row.addProperty("status", "outside readable executable sections");
            return callees;
        }
        // Do not follow arbitrary encrypted control flow over the entire image.
        // This spans fragmented unwind ranges, unlike truncating at the first .pdata end.
        Address low = at(Math.max(0, entryRva - 0x20000));
        Address high = at(Math.min(currentProgram.getMaxAddress().subtract(base), entryRva + 0x40000));
        AddressSet permitted = code.intersect(new AddressSet(low, high));
        DisassembleCommand cmd = new DisassembleCommand(entry, permitted, true);
        cmd.enableCodeAnalysis(false);
        cmd.applyTo(currentProgram, monitor);
        Function function = getFunctionAt(entry);
        if (function == null) function = createFunction(entry, names.get(entryRva));
        if (function == null) {
            row.addProperty("status", "could not create function");
            return callees;
        }
        if (names.containsKey(entryRva)) function.setName(names.get(entryRva), SourceType.USER_DEFINED);
        row.addProperty("name", function.getName());
        row.addProperty("body_bytes", function.getBody().getNumAddresses());
        row.addProperty("analysis_window", hex(rva(low)) + ".." + hex(rva(high)));
        JsonArray calls = new JsonArray();
        JsonArray indirectCalls = new JsonArray();
        JsonArray externalBranches = new JsonArray();
        StringBuilder assembly = new StringBuilder();
        InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
        int count = 0;
        while (instructions.hasNext()) {
            Instruction instruction = instructions.next();
            ++count;
            assembly.append(String.format("%08x: %s%n", rva(instruction.getAddress()), instruction));
            Address[] flows = instruction.getFlows();
            if (instruction.getFlowType().isCall()) {
                if (flows.length == 0) indirectCalls.add(hex(rva(instruction.getAddress())));
                for (Address target : flows) {
                    JsonObject call = new JsonObject();
                    call.addProperty("site", hex(rva(instruction.getAddress())));
                    call.addProperty("target", target.toString());
                    if (code.contains(target)) {
                        long next = rva(target);
                        call.addProperty("target_rva", hex(next));
                        callees.add(next);
                    }
                    calls.add(call);
                }
            } else if (instruction.getFlowType().isJump()) {
                for (Address target : flows) {
                    if (!permitted.contains(target)) externalBranches.add(target.toString());
                }
            }
        }
        row.addProperty("instruction_count", count);
        row.add("direct_calls", calls);
        row.add("indirect_call_sites", indirectCalls);
        row.add("branches_outside_analysis_window", externalBranches);
        String stem = String.format("%08x_%s", entryRva, function.getName().replaceAll("[^A-Za-z0-9_]", "_"));
        Files.writeString(output.resolve(stem + ".asm.txt"), assembly, StandardCharsets.UTF_8);
        DecompileResults decompiled = decompiler.decompileFunction(function, 40, monitor);
        row.addProperty("decompiler_message", decompiled.getErrorMessage());
        if (decompiled.decompileCompleted() && decompiled.getDecompiledFunction() != null) {
            String header = "/* OFFLINE RESEARCH PSEUDOCODE. Names/types are provisional.\n" +
                " * Function RVA " + hex(entryRva) + ", runtime base " + base + ".\n" +
                " * Validate against assembly/runtime evidence; incomplete or opaque bytes may mislead. */\n\n";
            Files.writeString(output.resolve(stem + ".c"), header + decompiled.getDecompiledFunction().getC(), StandardCharsets.UTF_8);
            row.addProperty("status", "decompiled; requires validation");
        } else row.addProperty("status", "decompilation incomplete");
        println(hex(entryRva) + " " + function.getName() + " instructions=" + count + " " + row.get("status"));
        return callees;
    }

    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("manifest.json outputDirectory [maxFunctions=140] [callDepth=1]");
        JsonObject manifest = JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject();
        output = Path.of(args[1]); Files.createDirectories(output);
        int maxFunctions = args.length > 2 ? Integer.parseInt(args[2]) : 140;
        int maxDepth = args.length > 3 ? Integer.parseInt(args[3]) : 1;
        long expectedBase = manifest.get("runtime_base").getAsLong();
        base = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(expectedBase);
        // BinaryLoader's baseAddr positions the bytes, not Program.imageBase metadata.
        // Validate loaded memory directly; never rebase already-relocated runtime bytes.
        if (currentProgram.getMemory().getBlock(base) == null ||
            !currentProgram.getMemory().getBlock(base).getStart().equals(base) ||
            !currentProgram.getMemory().contains(base.add(manifest.get("image_size").getAsLong() - 1)))
            throw new IllegalStateException("Imported memory does not match runtime export address/size");
        code = new AddressSet();
        for (JsonElement item : manifest.getAsJsonArray("sections")) {
            JsonObject section = item.getAsJsonObject();
            long start = section.get("rva").getAsLong(), size = section.get("size").getAsLong();
            if ((section.get("characteristics").getAsLong() & 0x20000000L) != 0 && size > 0)
                code.add(at(start), at(start + size - 1));
        }
        for (JsonElement item : manifest.getAsJsonArray("unreadable")) {
            JsonObject page = item.getAsJsonObject();
            long start = page.get("rva").getAsLong(), size = page.get("size").getAsLong();
            code.delete(at(start), at(start + size - 1));
        }
        LinkedHashSet<Long> seeds = new LinkedHashSet<>();
        for (JsonElement item : manifest.getAsJsonArray("named_seeds")) {
            JsonObject seed = item.getAsJsonObject();
            long value = Long.decode(seed.get("rva").getAsString());
            names.put(value, seed.get("name").getAsString()); seeds.add(value);
        }
        for (JsonElement value : manifest.getAsJsonArray("seeds")) seeds.add(value.getAsLong());
        for (Map.Entry<Long, String> item : names.entrySet()) {
            if (code.contains(at(item.getKey()))) createLabel(at(item.getKey()), item.getValue(), true);
        }
        DecompInterface decompiler = new DecompInterface();
        decompiler.setOptions(new DecompileOptions());
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) throw new IllegalStateException(decompiler.getLastMessage());
        ArrayDeque<long[]> queue = new ArrayDeque<>();
        for (long value : seeds) queue.add(new long[]{value, 0});
        try {
            while (!queue.isEmpty() && visited.size() < maxFunctions) {
                monitor.checkCancelled();
                long[] work = queue.remove();
                if (!visited.add(work[0])) continue;
                try {
                    List<Long> next = exportOne(work[0], (int)work[1], decompiler);
                    if (work[1] < maxDepth) for (long callee : next) queue.add(new long[]{callee, work[1] + 1});
                } catch (Exception error) {
                    printerr(hex(work[0]) + " " + error);
                    JsonObject row;
                    if (results.size() > 0 && results.get(results.size()-1).getAsJsonObject().get("rva").getAsString().equals(hex(work[0])))
                        row = results.get(results.size()-1).getAsJsonObject();
                    else {
                        row = new JsonObject(); row.addProperty("rva", hex(work[0])); results.add(row);
                    }
                    row.addProperty("status", "analysis exception");
                    row.addProperty("error", error.toString());
                }
                saveIndex(manifest, queue);
            }
            saveIndex(manifest, queue);
        } finally { decompiler.dispose(); }
        println("Saved " + results.size() + " function records to " + output);
    }
}
