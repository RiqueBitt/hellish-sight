import java.io.*;
import java.nio.file.*;

public class ExtractFromArc {
    public static void main(String[] args) throws Exception {
        if (args.length < 3) {
            System.out.println("Usage: ExtractFromArc <arc_file> <filename> <output_path>");
            System.exit(1);
        }
        String arcPath = args[0];
        String target = args[1];
        String outPath = args[2];

        DataInputStream dis = new DataInputStream(new FileInputStream(arcPath));
        int numFiles = dis.readInt();
        int foundOffset = -1, foundSize = -1;
        for (int i = 0; i < numFiles; i++) {
            String name = dis.readUTF();
            int offset = dis.readInt();
            int size = dis.readInt();
            if (name.startsWith("*")) name = name.substring(1);
            if (name.equals(target)) {
                foundOffset = offset;
                foundSize = size;
            }
        }
        dis.close();

        if (foundOffset == -1) {
            System.err.println("File not found in archive: " + target);
            System.exit(1);
        }

        RandomAccessFile raf = new RandomAccessFile(arcPath, "r");
        raf.seek(foundOffset);
        byte[] data = new byte[foundSize];
        raf.readFully(data);
        raf.close();
        Files.write(Paths.get(outPath), data);
        System.out.println("Extracted " + target + " (" + foundSize + " bytes) -> " + outPath);
    }
}
