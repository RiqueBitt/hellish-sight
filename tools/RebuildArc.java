import java.io.*;
import java.nio.file.*;
import java.util.*;

/**
 * Rebuilds a MediaWindows64.arc archive file by replacing specific SWF files
 * with updated versions from disk.
 *
 * Usage: RebuildArc <arc_file> <media_dir> [file1.swf file2.swf ...]
 *
 * If no specific files are given, replaces all SWF files found in media_dir.
 * The arc format is Java DataOutputStream style: big-endian ints, modified UTF-8 strings.
 *
 * Archive format:
 *   int: numberOfFiles
 *   For each file:
 *     UTF: filename (prefixed with '*' if compressed)
 *     int: offset into data section
 *     int: filesize
 *   Raw file data follows the header
 */
public class RebuildArc {
    public static void main(String[] args) throws Exception {
        if (args.length < 2) {
            System.out.println("Usage: RebuildArc <arc_file> <media_dir> [file1.swf file2.swf ...]");
            System.exit(1);
        }

        String arcPath = args[0];
        String mediaDir = args[1];
        Set<String> replaceFiles = new HashSet<>();
        for (int i = 2; i < args.length; i++) {
            replaceFiles.add(args[i]);
        }

        // Read the original archive
        DataInputStream dis = new DataInputStream(new FileInputStream(arcPath));
        int numberOfFiles = dis.readInt();

        System.out.println("Archive has " + numberOfFiles + " files");

        // Read the index
        List<String> filenames = new ArrayList<>();
        List<Integer> offsets = new ArrayList<>();
        List<Integer> sizes = new ArrayList<>();
        List<Boolean> compressed = new ArrayList<>();

        for (int i = 0; i < numberOfFiles; i++) {
            String name = dis.readUTF();
            int offset = dis.readInt();
            int size = dis.readInt();

            boolean isCompressed = false;
            if (name.startsWith("*")) {
                name = name.substring(1);
                isCompressed = true;
            }

            filenames.add(name);
            offsets.add(offset);
            sizes.add(size);
            compressed.add(isCompressed);
        }

        // The header size is the current position - data offsets are relative to file start
        // Read the entire file to get the raw data
        dis.close();
        byte[] arcData = Files.readAllBytes(Paths.get(arcPath));

        // Build replacement data map
        // For each file, either use the replacement or the original data
        List<byte[]> fileData = new ArrayList<>();
        int replacedCount = 0;

        for (int i = 0; i < numberOfFiles; i++) {
            String name = filenames.get(i);
            String simpleName = name.contains("\\") ? name.substring(name.lastIndexOf('\\') + 1) : name;

            boolean shouldReplace = false;
            if (replaceFiles.isEmpty()) {
                // Replace all SWFs found in mediaDir
                File diskFile = new File(mediaDir, simpleName);
                if (diskFile.exists() && simpleName.endsWith(".swf")) {
                    shouldReplace = true;
                }
            } else {
                shouldReplace = replaceFiles.contains(simpleName);
            }

            if (shouldReplace) {
                File diskFile = new File(mediaDir, simpleName);
                if (diskFile.exists()) {
                    byte[] newData = Files.readAllBytes(diskFile.toPath());
                    fileData.add(newData);
                    // Replaced files are NOT compressed (our SWFs are uncompressed)
                    compressed.set(i, false);
                    sizes.set(i, newData.length);
                    System.out.println("  Replacing: " + name + " (" + newData.length + " bytes)");
                    replacedCount++;
                } else {
                    System.err.println("  WARNING: " + diskFile + " not found, keeping original");
                    byte[] original = new byte[sizes.get(i)];
                    System.arraycopy(arcData, offsets.get(i), original, 0, sizes.get(i));
                    fileData.add(original);
                }
            } else {
                // Keep original data
                byte[] original = new byte[sizes.get(i)];
                System.arraycopy(arcData, offsets.get(i), original, 0, sizes.get(i));
                fileData.add(original);
            }
        }

        if (replacedCount == 0) {
            System.out.println("No files were replaced!");
            System.exit(1);
        }

        // Calculate new header size to compute data offsets
        // Header: 4 bytes (numFiles) + for each file: (2 + UTF8 length + 4 + 4) bytes
        ByteArrayOutputStream headerBuf = new ByteArrayOutputStream();
        DataOutputStream headerDos = new DataOutputStream(headerBuf);
        headerDos.writeInt(numberOfFiles);
        for (int i = 0; i < numberOfFiles; i++) {
            String name = filenames.get(i);
            if (compressed.get(i)) {
                name = "*" + name;
            }
            headerDos.writeUTF(name);
            headerDos.writeInt(0); // placeholder offset
            headerDos.writeInt(0); // placeholder size
        }
        headerDos.flush();
        int headerSize = headerBuf.size();

        // Now compute real offsets
        int currentOffset = headerSize;
        List<Integer> newOffsets = new ArrayList<>();
        for (int i = 0; i < numberOfFiles; i++) {
            newOffsets.add(currentOffset);
            currentOffset += fileData.get(i).length;
        }

        // Write the final archive
        String outputPath = arcPath; // overwrite in place
        DataOutputStream dos = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(outputPath)));
        dos.writeInt(numberOfFiles);
        for (int i = 0; i < numberOfFiles; i++) {
            String name = filenames.get(i);
            if (compressed.get(i)) {
                name = "*" + name;
            }
            dos.writeUTF(name);
            dos.writeInt(newOffsets.get(i));
            dos.writeInt(fileData.get(i).length);
        }

        // Write file data
        for (int i = 0; i < numberOfFiles; i++) {
            dos.write(fileData.get(i));
        }

        dos.flush();
        dos.close();

        System.out.println("Rebuilt archive: " + outputPath + " (" + replacedCount + " files replaced)");
    }
}
