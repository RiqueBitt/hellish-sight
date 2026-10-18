import java.io.*;
import java.nio.file.*;

public class ListArc {
    public static void main(String[] args) throws Exception {
        DataInputStream dis = new DataInputStream(new FileInputStream(args[0]));
        int numFiles = dis.readInt();
        System.out.println("Files: " + numFiles);
        for (int i = 0; i < numFiles; i++) {
            String name = dis.readUTF();
            int offset = dis.readInt();
            int size = dis.readInt();
            if (name.contains("ogo") || name.contains("skin") || name.contains("Menu") || name.contains("platform")) {
                System.out.println("  >>> " + name + " offset=" + offset + " size=" + size);
            }
        }
        dis.close();
    }
}
