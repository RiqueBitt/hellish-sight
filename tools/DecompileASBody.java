import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.exporters.modes.ScriptExportMode;
import com.jpexs.decompiler.flash.exporters.settings.ScriptExportSettings;
import com.jpexs.decompiler.flash.configuration.Configuration;
import java.io.*;
import java.util.*;

/**
 * Exports all ActionScript 3 source from a SWF to a directory.
 * Usage: DecompileASBody <swf-file> [output-dir]
 */
public class DecompileASBody {
    public static void main(String[] args) throws Exception {
        if (args.length < 1) {
            System.out.println("Usage: DecompileASBody <swf-file> [output-dir]");
            return;
        }

        Configuration.autoDeobfuscate.set(false);

        String path = args[0];
        String outDir = args.length > 1 ? args[1] : "as_output";

        SWF swf = new SWF(new FileInputStream(path), false);

        File out = new File(outDir);
        out.mkdirs();

        ScriptExportSettings settings = new ScriptExportSettings(ScriptExportMode.AS, false, false, false, false);
        swf.exportActionScript(null, outDir, settings, false, null);

        System.out.println("Export complete. Files:");
        listFiles(new File(outDir), "");
    }

    static void listFiles(File dir, String prefix) {
        File[] files = dir.listFiles();
        if (files == null) return;
        Arrays.sort(files);
        for (File f : files) {
            if (f.isDirectory()) {
                listFiles(f, prefix + f.getName() + "/");
            } else {
                System.out.println("  " + prefix + f.getName() + " (" + f.length() + " bytes)");
            }
        }
    }
}
