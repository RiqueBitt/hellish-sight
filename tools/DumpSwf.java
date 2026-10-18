import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.tags.base.*;
import com.jpexs.decompiler.flash.types.*;
import java.io.*;
import java.util.*;

public class DumpSwf {
    public static void main(String[] args) throws Exception {
        String path = args[0];
        SWF swf = new SWF(new FileInputStream(path), false);

        System.out.println("=== Top-level Tags ===");
        int idx = 0;
        for (Tag tag : swf.getTags()) {
            System.out.println("[" + idx + "] " + tag.getClass().getSimpleName() + " - " + tag);
            dumpPlaceObject(tag, "  ");

            if (tag instanceof DefineSpriteTag) {
                DefineSpriteTag sprite = (DefineSpriteTag) tag;
                System.out.println("  spriteId=" + sprite.spriteId + " frames=" + sprite.frameCount);
                int subIdx = 0;
                for (Tag sub : sprite.getTags()) {
                    System.out.println("  [" + subIdx + "] " + sub.getClass().getSimpleName() + " - " + sub);
                    dumpPlaceObject(sub, "    ");
                    subIdx++;
                }
            }
            idx++;
        }
    }

    static void dumpPlaceObject(Tag tag, String indent) {
        if (tag instanceof PlaceObjectTypeTag) {
            PlaceObjectTypeTag po = (PlaceObjectTypeTag) tag;
            System.out.println(indent + "depth=" + po.getDepth() + " charId=" + po.getCharacterId()
                + " name=" + po.getInstanceName() + " matrix=" + po.getMatrix());
        }
    }
}
