import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.tags.base.*;
import com.jpexs.decompiler.flash.types.*;
import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.*;
import java.util.*;

/**
 * Adds hardcore heart frames to the health sprite in skinHDHud.swf.
 *
 * The health sprite (FJ_Health) has 14 frames for normal/poison/wither states.
 * This tool adds 10 new frames (15-24) for hardcore variants:
 *   Frame 15: Health_Empty_Hardcore (hardcore background)
 *   Frame 16: Health_Empty_Flash_Hardcore (hardcore background flash)
 *   Frame 17: Health_Full_Hardcore (hardcore bg + full)
 *   Frame 18: Health_Half_Hardcore (hardcore bg + half)
 *   Frame 19: Health_Full_Flash_Hardcore
 *   Frame 20: Health_Half_Flash_Hardcore
 *   Frame 21: Health_Full_Poison_Hardcore (hardcore bg + poison full)
 *   Frame 22: Health_Half_Poison_Hardcore
 *   Frame 23: Health_Full_Poison_Flash_Hardcore
 *   Frame 24: Health_Half_Poison_Flash_Hardcore
 *
 * Usage: AddHardcoreHearts <skinHDHud.swf> <textures-dir> <output.swf>
 *
 * The textures-dir should contain the hardcore PNGs:
 *   Health_Background_Hardcore.png, Health_Background_Hardcore_Flash.png,
 *   Health_Full_Hardcore.png, Health_Half_Hardcore.png,
 *   Health_Full_Flash_Hardcore.png, Health_Half_Flash_Hardcore.png,
 *   Health_Full_Poison_Hardcore.png, Health_Half_Poison_Hardcore.png,
 *   Health_Full_Poison_Flash_Hardcore.png, Health_Half_Poison_Flash_Hardcore.png
 */
public class AddHardcoreHearts {
    public static void main(String[] args) throws Exception {
        if (args.length < 3) {
            System.out.println("Usage: AddHardcoreHearts <skinHDHud.swf> <textures-dir> <output.swf>");
            System.exit(1);
        }

        String swfPath = args[0];
        String texturesDir = args[1];
        String outputPath = args[2];

        System.out.println("Loading SWF: " + swfPath);
        SWF swf = new SWF(new FileInputStream(swfPath), false);

        // Find the FJ_Health sprite by its SymbolClass name
        Map<Integer, String> symbolNames = new HashMap<>();
        for (Tag tag : swf.getTags()) {
            if (tag instanceof SymbolClassTag) {
                SymbolClassTag sym = (SymbolClassTag) tag;
                for (int i = 0; i < sym.tags.size(); i++) {
                    symbolNames.put(sym.tags.get(i), sym.names.get(i));
                }
            }
        }

        // Find the health sprite
        DefineSpriteTag healthSprite = null;
        MATRIX existingMatrix = null;  // Will capture from existing PlaceObject3
        for (Tag tag : swf.getTags()) {
            if (tag instanceof DefineSpriteTag) {
                DefineSpriteTag sprite = (DefineSpriteTag) tag;
                String sym = symbolNames.getOrDefault(sprite.spriteId, "");
                if (sym.contains("FJ_Health") && !sym.contains("Absorb") && !sym.contains("Horse")) {
                    healthSprite = sprite;
                    System.out.println("Found health sprite: id=" + sprite.spriteId + " sym=" + sym + " frames=" + sprite.frameCount);
                    // Grab the matrix from the first PlaceObject3 in the sprite (Health_Background)
                    for (Tag sub : sprite.getTags()) {
                        if (sub instanceof PlaceObject3Tag) {
                            PlaceObject3Tag po = (PlaceObject3Tag) sub;
                            if (po.matrix != null) {
                                existingMatrix = po.matrix;
                                System.out.println("Captured matrix: " + existingMatrix);
                                break;
                            }
                        }
                    }
                    break;
                }
            }
        }

        if (healthSprite == null) {
            System.err.println("ERROR: Could not find FJ_Health sprite!");
            System.exit(1);
        }

        // Find the highest character ID in the SWF to avoid conflicts
        int maxCharId = 0;
        for (Tag tag : swf.getTags()) {
            if (tag instanceof CharacterTag) {
                int id = ((CharacterTag) tag).getCharacterId();
                if (id > maxCharId) maxCharId = id;
            }
        }
        System.out.println("Max existing character ID: " + maxCharId);

        // Load hardcore textures and create DefineBitsLossless2 tags
        // We need to add these as top-level tags in the SWF, then reference them
        // via PlaceObject in the health sprite frames

        // Map: texture name -> charId for the new bitmap
        Map<String, Integer> textureIds = new LinkedHashMap<>();
        String[] textureNames = {
            "Health_Background_Hardcore",
            "Health_Background_Hardcore_Flash",
            "Health_Full_Hardcore",
            "Health_Half_Hardcore",
            "Health_Full_Flash_Hardcore",
            "Health_Half_Flash_Hardcore",
            "Health_Full_Poison_Hardcore",
            "Health_Half_Poison_Hardcore",
            "Health_Full_Poison_Flash_Hardcore",
            "Health_Half_Poison_Flash_Hardcore"
        };

        // Find where to insert new tags (before SymbolClassTag)
        int insertIndex = -1;
        for (int i = 0; i < swf.getTags().size(); i++) {
            if (swf.getTags().get(i) instanceof SymbolClassTag) {
                insertIndex = i;
                break;
            }
        }

        int nextCharId = maxCharId + 1;
        SymbolClassTag symbolClass = null;
        for (Tag tag : swf.getTags()) {
            if (tag instanceof SymbolClassTag) {
                symbolClass = (SymbolClassTag) tag;
                break;
            }
        }

        for (String texName : textureNames) {
            String pngFile = texturesDir + "/" + texName + ".png";
            File f = new File(pngFile);
            if (!f.exists()) {
                System.err.println("ERROR: Missing texture: " + pngFile);
                System.exit(1);
            }

            BufferedImage img = ImageIO.read(f);
            System.out.println("  Loading " + texName + " (" + img.getWidth() + "x" + img.getHeight() + ")");

            // Create DefineBitsLossless2 tag
            DefineBitsLossless2Tag bmp = new DefineBitsLossless2Tag(swf);
            bmp.characterID = nextCharId;

            // Convert image to PNG bytes for setImage
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            ImageIO.write(img, "PNG", baos);
            bmp.setImage(baos.toByteArray());

            textureIds.put(texName, nextCharId);

            // Insert before SymbolClass
            swf.addTag(insertIndex, bmp);
            insertIndex++;

            // Add to SymbolClass so it can be referenced by name
            symbolClass.tags.add(nextCharId);
            symbolClass.names.add(texName);

            nextCharId++;
        }

        System.out.println("Added " + textureIds.size() + " hardcore heart bitmaps");

        // Mark SymbolClass as modified so new entries get serialized
        symbolClass.setModified(true);

        // Now add 10 new frames to the health sprite
        // Current structure: 14 frames, each has RemoveObject2, PlaceObject3(bg), PlaceObject3(heart), ShowFrame
        // We follow the same pattern

        // Frame structure for each hardcore frame:
        // 1. FrameLabelTag (name)
        // 2. RemoveObject2 depth 1 (remove old bg) - only if not first frame
        // 3. PlaceObject3 cls=<bg> depth 1
        // 4. RemoveObject2 depth 2 (remove old heart) - only if heart present
        // 5. PlaceObject3 cls=<heart> depth 2 - if not empty
        // 6. ShowFrameTag

        // The frame structure from the existing SWF:
        // Each frame after the first uses RemoveObject2 to clear the previous frame's objects
        // then places new objects

        // Define our 10 new frames
        String[][] hardcoreFrames = {
            // {label, bgTexture, heartTexture}  (heartTexture null for empty)
            {"Health_Empty_Hardcore",            "Health_Background_Hardcore",       null},
            {"Health_Empty_Flash_Hardcore",      "Health_Background_Hardcore_Flash", null},
            {"Health_Full_Hardcore",             "Health_Background_Hardcore",       "Health_Full_Hardcore"},
            {"Health_Half_Hardcore",             null,                               "Health_Half_Hardcore"},  // bg continues from prev
            {"Health_Full_Flash_Hardcore",       "Health_Background_Hardcore_Flash", "Health_Full_Flash_Hardcore"},
            {"Health_Half_Flash_Hardcore",       null,                               "Health_Half_Flash_Hardcore"},
            {"Health_Full_Poison_Hardcore",      "Health_Background_Hardcore",       "Health_Full_Poison_Hardcore"},
            {"Health_Half_Poison_Hardcore",      null,                               "Health_Half_Poison_Hardcore"},
            {"Health_Full_Poison_Flash_Hardcore","Health_Background_Hardcore_Flash", "Health_Full_Poison_Flash_Hardcore"},
            {"Health_Half_Poison_Flash_Hardcore",null,                               "Health_Half_Poison_Flash_Hardcore"},
        };

        // Actually, looking at the existing structure more carefully:
        // Frame 1 (Health_Empty): PlaceObject3 bg@1, ShowFrame
        // Frame 2 (Health_Empty_Flash): Remove@1, PlaceObject3 bg_flash@1, ShowFrame
        // Frame 3 (Health_Full): Remove@1, PlaceObject3 bg@1, PlaceObject3 full@2, ShowFrame
        // Frame 4 (Health_Half): PlaceObject3 half@2 (replaces heart, bg continues), ShowFrame
        //   Wait - frame 4 reuses bg from frame 3. Let me re-check...

        // Looking at the dump:
        // [8] FrameLabel "Health_Full"
        // [9] PlaceObject3 cls=Health_Background dpt=1
        // [10] PlaceObject3 cls=Health_Full dpt=2
        // [11] ShowFrame
        // [12] RemoveObject2 dpt=2
        // [13] FrameLabel "Health_Half"
        // [14] PlaceObject3 cls=Health_Half dpt=2
        // [15] ShowFrame (bg from frame 3 still on depth 1)
        // [16] RemoveObject2 dpt=1
        // [17] RemoveObject2 dpt=2
        // [18] FrameLabel "Health_Full_Flash"
        // [19] PlaceObject3 cls=Health_Background_Flash dpt=1
        // [20] PlaceObject3 cls=Health_Full_Flash dpt=2
        // [21] ShowFrame

        // So the pattern is:
        // - When bg changes: remove depth 1, place new bg at depth 1
        // - When heart changes: remove depth 2, place new heart at depth 2
        // - Half frames reuse the bg from the previous Full frame

        // For the last existing frame (14, Health_Half_Wither_Flash), it has objects at depth 1 and 2.
        // We need to start our new frames by removing those.

        // Let me follow the exact same pattern as the existing frames, starting from a clean slate
        // after the last existing frame.

        if (existingMatrix == null) {
            System.err.println("ERROR: Could not find matrix from existing health sprite!");
            System.exit(1);
        }

        // Build the new tags to append
        List<Tag> newTags = new ArrayList<>();

        // Frame structure follows the original pattern:
        //   Empty:      remove@1, place bg@1                    (depth 2 empty)
        //   EmptyFlash: remove@1, place flashbg@1               (depth 2 empty)
        //   Full:       remove@1, place bg@1, place full@2      (ADD heart, no remove@2)
        //   Half:       remove@2, place half@2                  (REPLACE heart, bg continues)
        //   FullFlash:  remove@1, place flashbg@1, remove@2, place fullflash@2
        //   HalfFlash:  remove@2, place halfflash@2             (bg continues)
        //   (Poison/Wither follow the same Full/Half/FullFlash/HalfFlash pattern)

        //                              label,                          rmBg, bgTex,                     rmHrt, hrtTex
        // Frame 15: Empty - remove both from frame 14
        addFrame(newTags, swf, healthSprite, "Health_Empty_Hardcore",
            true, "Health_Background_Hardcore",
            true, null, existingMatrix);

        // Frame 16: EmptyFlash
        addFrame(newTags, swf, healthSprite, "Health_Empty_Flash_Hardcore",
            true, "Health_Background_Hardcore_Flash",
            false, null, existingMatrix);

        // Frame 17: Full - bg changes, heart ADDED (no remove@2, depth 2 was empty)
        addFrame(newTags, swf, healthSprite, "Health_Full_Hardcore",
            true, "Health_Background_Hardcore",
            false, "Health_Full_Hardcore", existingMatrix);

        // Frame 18: Half - heart REPLACED (remove@2, then place), bg continues
        addFrame(newTags, swf, healthSprite, "Health_Half_Hardcore",
            false, null,
            true, "Health_Half_Hardcore", existingMatrix);

        // Frame 19: FullFlash - both change
        addFrame(newTags, swf, healthSprite, "Health_Full_Flash_Hardcore",
            true, "Health_Background_Hardcore_Flash",
            true, "Health_Full_Flash_Hardcore", existingMatrix);

        // Frame 20: HalfFlash - heart replaced, bg continues
        addFrame(newTags, swf, healthSprite, "Health_Half_Flash_Hardcore",
            false, null,
            true, "Health_Half_Flash_Hardcore", existingMatrix);

        // Frame 21: FullPoison - both change
        addFrame(newTags, swf, healthSprite, "Health_Full_Poison_Hardcore",
            true, "Health_Background_Hardcore",
            true, "Health_Full_Poison_Hardcore", existingMatrix);

        // Frame 22: HalfPoison - heart replaced, bg continues
        addFrame(newTags, swf, healthSprite, "Health_Half_Poison_Hardcore",
            false, null,
            true, "Health_Half_Poison_Hardcore", existingMatrix);

        // Frame 23: FullPoisonFlash - both change
        addFrame(newTags, swf, healthSprite, "Health_Full_Poison_Flash_Hardcore",
            true, "Health_Background_Hardcore_Flash",
            true, "Health_Full_Poison_Flash_Hardcore", existingMatrix);

        // Frame 24: HalfPoisonFlash - heart replaced, bg continues
        addFrame(newTags, swf, healthSprite, "Health_Half_Poison_Flash_Hardcore",
            false, null,
            true, "Health_Half_Poison_Flash_Hardcore", existingMatrix);

        // Append new tags to sprite using addTag
        int spriteInsertIdx = healthSprite.getTags().size();
        for (Tag newTag : newTags) {
            healthSprite.addTag(spriteInsertIdx, newTag);
            spriteInsertIdx++;
        }
        healthSprite.frameCount += 10;

        System.out.println("Added 10 hardcore frames. New frame count: " + healthSprite.frameCount);

        // Save
        System.out.println("Saving modified SWF: " + outputPath);
        try (FileOutputStream fos = new FileOutputStream(outputPath)) {
            swf.saveTo(fos);
        }
        System.out.println("Done!");
    }

    /**
     * Adds tags for a single frame in the health sprite.
     * Uses the existingMatrix (cloned from existing PlaceObject3) to ensure correct encoding.
     */
    /**
     * @param removeBg    emit RemoveObject2 for depth 1 before placing bg
     * @param bgTexture   if non-null, place this at depth 1
     * @param removeHeart emit RemoveObject2 for depth 2 before placing heart
     * @param heartTexture if non-null, place this at depth 2
     */
    static void addFrame(List<Tag> tags, SWF swf, DefineSpriteTag parent,
                         String label,
                         boolean removeBg, String bgTexture,
                         boolean removeHeart, String heartTexture,
                         MATRIX existingMatrix) {

        // Frame label
        FrameLabelTag frameLabel = new FrameLabelTag(swf);
        frameLabel.name = label;
        frameLabel.setTimelined(parent);
        tags.add(frameLabel);

        // Background at depth 1
        if (removeBg) {
            RemoveObject2Tag rem = new RemoveObject2Tag(swf);
            rem.depth = 1;
            rem.setTimelined(parent);
            tags.add(rem);
        }
        if (bgTexture != null) {
            PlaceObject3Tag placeBg = new PlaceObject3Tag(swf);
            placeBg.depth = 1;
            placeBg.className = bgTexture;
            placeBg.placeFlagHasMatrix = true;
            placeBg.matrix = cloneMatrix(existingMatrix);
            placeBg.placeFlagHasClassName = true;
            placeBg.placeFlagHasCharacter = false;
            placeBg.setTimelined(parent);
            tags.add(placeBg);
        }

        // Heart at depth 2
        if (removeHeart) {
            RemoveObject2Tag rem = new RemoveObject2Tag(swf);
            rem.depth = 2;
            rem.setTimelined(parent);
            tags.add(rem);
        }
        if (heartTexture != null) {
            PlaceObject3Tag placeHeart = new PlaceObject3Tag(swf);
            placeHeart.depth = 2;
            placeHeart.className = heartTexture;
            placeHeart.placeFlagHasMatrix = true;
            placeHeart.matrix = cloneMatrix(existingMatrix);
            placeHeart.placeFlagHasClassName = true;
            placeHeart.placeFlagHasCharacter = false;
            placeHeart.setTimelined(parent);
            tags.add(placeHeart);
        }

        // ShowFrame
        ShowFrameTag showFrame = new ShowFrameTag(swf);
        showFrame.setTimelined(parent);
        tags.add(showFrame);
    }

    static MATRIX cloneMatrix(MATRIX src) {
        MATRIX m = new MATRIX();
        m.hasScale = src.hasScale;
        m.scaleX = src.scaleX;
        m.scaleY = src.scaleY;
        m.hasRotate = src.hasRotate;
        m.rotateSkew0 = src.rotateSkew0;
        m.rotateSkew1 = src.rotateSkew1;
        m.translateX = src.translateX;
        m.translateY = src.translateY;
        return m;
    }
}
