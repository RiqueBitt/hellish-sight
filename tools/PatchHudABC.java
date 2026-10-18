import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.abc.*;
import com.jpexs.decompiler.flash.abc.avm2.AVM2ConstantPool;
import com.jpexs.decompiler.flash.abc.types.*;
import com.jpexs.decompiler.flash.abc.types.traits.*;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.configuration.Configuration;
import java.io.*;
import java.util.*;

/**
 * Patches the HUD SWF ABC bytecode directly to add hardcore hearts support.
 *
 * This uses raw byte patching with manual jump offset fixups, avoiding the
 * AVM2Code instruction list approach which fails to adjust existing jump
 * offsets when bytes are inserted.
 *
 * Changes:
 * 1. Adds m_bHardcore:Boolean instance variable to Hud class
 * 2. Adds SetHardcore(Boolean) method
 * 3. Modifies SetHealth to check m_bHardcore and offset heart frames
 *
 * Usage: PatchHudABC <hud.swf> <output.swf>
 */
public class PatchHudABC {

    // AVM2 opcodes that have s24 jump offset as first operand
    static final Set<Integer> JUMP_OPCODES = Set.of(
        0x10, // jump
        0x11, // iftrue
        0x12, // iffalse
        0x13, // ifeq
        0x14, // ifne
        0x15, // iflt
        0x16, // ifle
        0x17, // ifgt
        0x18, // ifge
        0x19, // ifstricteq
        0x1A, // ifstrictne
        0x0C, // ifnlt
        0x0D, // ifnle
        0x0E, // ifngt
        0x0F  // ifnge
    );

    public static void main(String[] args) throws Exception {
        if (args.length < 2) {
            System.out.println("Usage: PatchHudABC <hud.swf> <output.swf>");
            return;
        }

        Configuration.autoDeobfuscate.set(false);

        String inputPath = args[0];
        String outputPath = args[1];

        System.out.println("Loading SWF: " + inputPath);
        SWF swf = new SWF(new FileInputStream(inputPath), false);

        for (Tag tag : swf.getTags()) {
            if (!(tag instanceof DoABC2Tag)) continue;
            DoABC2Tag abcTag = (DoABC2Tag) tag;
            ABC abc = abcTag.getABC();
            AVM2ConstantPool cp = abc.constants;

            // Find the Hud class
            int hudClassIdx = -1;
            for (int c = 0; c < abc.instance_info.size(); c++) {
                Multiname mn = cp.getMultiname(abc.instance_info.get(c).name_index);
                String name = mn.name_index > 0 ? cp.getString(mn.name_index) : "";
                if ("Hud".equals(name)) {
                    hudClassIdx = c;
                    break;
                }
            }

            if (hudClassIdx < 0) {
                System.err.println("ERROR: Could not find Hud class");
                System.exit(1);
            }

            System.out.println("Found Hud class at index " + hudClassIdx);

            InstanceInfo hudInstance = abc.instance_info.get(hudClassIdx);

            // === Step 1: Add m_bHardcore Boolean member variable ===

            int strHardcore = ensureString(cp, "m_bHardcore");
            int mnBoolean = findMultinameByName(cp, "Boolean");
            if (mnBoolean < 0) {
                System.err.println("ERROR: Could not find Boolean multiname");
                System.exit(1);
            }

            // Find the private namespace for the Hud class (same as m_bWithered uses)
            int privateNs = -1;
            for (Trait t : hudInstance.instance_traits.traits) {
                Multiname tMn = cp.getMultiname(t.name_index);
                String tName = tMn.name_index > 0 ? cp.getString(tMn.name_index) : "";
                if ("m_bWithered".equals(tName)) {
                    privateNs = tMn.namespace_index;
                    break;
                }
            }

            if (privateNs < 0) {
                System.err.println("ERROR: Could not find m_bWithered private namespace");
                System.exit(1);
            }

            // Create QName multiname for m_bHardcore
            int mnHardcore = addQName(cp, strHardcore, privateNs);

            // Add trait
            TraitSlotConst hardcoreSlot = new TraitSlotConst();
            hardcoreSlot.name_index = mnHardcore;
            hardcoreSlot.kindType = Trait.TRAIT_SLOT;
            hardcoreSlot.type_index = mnBoolean;
            hardcoreSlot.slot_id = 0; // auto-assign
            hudInstance.instance_traits.traits.add(hardcoreSlot);
            System.out.println("Added m_bHardcore member variable");

            // === Step 2: Add SetHardcore method ===
            int strSetHardcore = ensureString(cp, "SetHardcore");

            // Find public namespace (same as SetHealth uses)
            int publicNs = -1;
            for (Trait t : hudInstance.instance_traits.traits) {
                if (t instanceof TraitMethodGetterSetter) {
                    Multiname tMn = cp.getMultiname(t.name_index);
                    String tName = tMn.name_index > 0 ? cp.getString(tMn.name_index) : "";
                    if ("SetHealth".equals(tName)) {
                        publicNs = tMn.namespace_index;
                        break;
                    }
                }
            }

            if (publicNs < 0) {
                System.err.println("ERROR: Could not find SetHealth public namespace");
                System.exit(1);
            }

            int mnSetHardcore = addQName(cp, strSetHardcore, publicNs);

            // Create method info for SetHardcore(Boolean):void
            int retTypeVoid = findMultinameByName(cp, "void");
            if (retTypeVoid < 0) retTypeVoid = 0;
            MethodInfo setHardcoreMethod = new MethodInfo(
                new int[]{mnBoolean}, retTypeVoid, 0, 0, null, null
            );
            abc.method_info.add(setHardcoreMethod);
            int setHardcoreMethodIdx = abc.method_info.size() - 1;

            // Find an existing instance method body to copy scope depth from
            int refInitScope = 10;
            int refMaxScope = 11;
            for (Trait t : hudInstance.instance_traits.traits) {
                if (t instanceof TraitMethodGetterSetter) {
                    MethodBody refBody = abc.findBody(((TraitMethodGetterSetter) t).method_info);
                    if (refBody != null) {
                        refInitScope = refBody.init_scope_depth;
                        refMaxScope = refBody.max_scope_depth;
                        break;
                    }
                }
            }

            // Create method body with raw bytes:
            // D0        getlocal0
            // 30        pushscope
            // D0        getlocal0
            // D1        getlocal1
            // 61 XX XX  setproperty m_bHardcore (u30 multiname)
            // 47        returnvoid
            MethodBody setHardcoreBody = new MethodBody();
            setHardcoreBody.method_info = setHardcoreMethodIdx;
            setHardcoreBody.max_stack = 2;
            setHardcoreBody.max_regs = 2;
            setHardcoreBody.init_scope_depth = refInitScope;
            setHardcoreBody.max_scope_depth = refMaxScope;

            byte[] mnHardcoreU30 = encodeU30(mnHardcore);
            byte[] shBytes = new byte[4 + 1 + mnHardcoreU30.length + 1];
            int pos = 0;
            shBytes[pos++] = (byte) 0xD0; // getlocal0
            shBytes[pos++] = (byte) 0x30; // pushscope
            shBytes[pos++] = (byte) 0xD0; // getlocal0
            shBytes[pos++] = (byte) 0xD1; // getlocal1
            shBytes[pos++] = (byte) 0x61; // setproperty
            System.arraycopy(mnHardcoreU30, 0, shBytes, pos, mnHardcoreU30.length);
            pos += mnHardcoreU30.length;
            shBytes[pos++] = (byte) 0x47; // returnvoid

            setHardcoreBody.setCodeBytes(shBytes);
            abc.bodies.add(setHardcoreBody);

            // Create trait for SetHardcore method
            TraitMethodGetterSetter setHardcoreTrait = new TraitMethodGetterSetter();
            setHardcoreTrait.name_index = mnSetHardcore;
            setHardcoreTrait.kindType = Trait.TRAIT_METHOD;
            setHardcoreTrait.method_info = setHardcoreMethodIdx;
            hudInstance.instance_traits.traits.add(setHardcoreTrait);
            System.out.println("Added SetHardcore method");

            // === Step 3: Patch SetHealth raw bytes ===
            for (Trait t : hudInstance.instance_traits.traits) {
                if (!(t instanceof TraitMethodGetterSetter)) continue;
                Multiname tMn = cp.getMultiname(t.name_index);
                String tName = tMn.name_index > 0 ? cp.getString(tMn.name_index) : "";
                if (!"SetHealth".equals(tName)) continue;

                TraitMethodGetterSetter tm = (TraitMethodGetterSetter) t;
                MethodBody body = abc.findBody(tm.method_info);
                if (body == null) continue;

                System.out.println("Patching SetHealth method body...");
                patchSetHealthRawBytes(body, mnHardcore);

                // Bump max_stack if needed
                if (body.max_stack < 3) body.max_stack = 3;
                break;
            }

            abcTag.setModified(true);
            break;
        }

        System.out.println("Saving to: " + outputPath);
        try (FileOutputStream fos = new FileOutputStream(outputPath)) {
            swf.saveTo(fos);
        }
        System.out.println("Done!");
    }

    /**
     * Patches SetHealth by inserting raw bytes after the setlocal 7 instruction.
     *
     * The patch implements:
     *   if (m_bHardcore) {
     *       if (frame >= 11) frame += 6;   // wither -> hardcore normal
     *       else frame += 14;              // normal/poison -> hardcore
     *   }
     */
    static void patchSetHealthRawBytes(MethodBody body, int mnHardcore) {
        byte[] code = body.getCodeBytes();

        // Find "setlocal 7" (0x63 0x07) after byte offset ~150
        int insertAt = -1;
        for (int i = 150; i < code.length - 1; i++) {
            if ((code[i] & 0xFF) == 0x63 && (code[i + 1] & 0xFF) == 0x07) {
                insertAt = i + 2; // insert AFTER setlocal 7
                break;
            }
        }

        if (insertAt < 0) {
            System.err.println("ERROR: Could not find setlocal 7 in SetHealth bytecode");
            System.exit(1);
        }
        System.out.println("Inserting patch at byte offset " + insertAt);

        // Build the patch bytes manually.
        // Layout with byte sizes:
        //
        // D0              getlocal0                          (1)
        // 66 XX..         getproperty m_bHardcore (u30)     (1 + u30len)
        // 12 YY YY YY    iffalse <skip to end>             (4)
        // 62 07           getlocal 7                        (2)
        // 24 0B           pushbyte 11                       (2)
        // 15 ZZ ZZ ZZ    iflt <to add14 block>             (4)
        // 62 07           getlocal 7                        (2)
        // 24 06           pushbyte 6                        (2)
        // A0              add                               (1)
        // 73              convert_i                         (1)
        // 63 07           setlocal 7                        (2)
        // 10 WW WW WW    jump <to end>                     (4)
        // --- add14 block ---
        // 62 07           getlocal 7                        (2)
        // 24 0E           pushbyte 14                       (2)
        // A0              add                               (1)
        // 73              convert_i                         (1)
        // 63 07           setlocal 7                        (2)
        // --- end ---

        byte[] mnU30 = encodeU30(mnHardcore);
        int mnU30Len = mnU30.length;

        // Calculate block sizes for jump offset computation
        // iffalse is at offset: 1 + 1 + mnU30Len = (2 + mnU30Len) from patch start
        // iffalse instruction ends at: (2 + mnU30Len) + 4 = (6 + mnU30Len)
        //
        // After iffalse: getlocal7(2) + pushbyte11(2) + iflt(4) = 8 bytes
        // iflt is at offset: (6 + mnU30Len) + 4 = (10 + mnU30Len) from patch start
        // iflt instruction ends at: (10 + mnU30Len) + 4 = (14 + mnU30Len)
        //
        // add6 block: getlocal7(2) + pushbyte6(2) + add(1) + convert_i(1) + setlocal7(2) + jump(4) = 12 bytes
        // jump is at offset: (14 + mnU30Len) + 8 = (22 + mnU30Len) from patch start
        // jump instruction ends at: (22 + mnU30Len) + 4 = (26 + mnU30Len)
        //
        // add14 block starts at offset: (26 + mnU30Len) from patch start
        // add14 block: getlocal7(2) + pushbyte14(2) + add(1) + convert_i(1) + setlocal7(2) = 8 bytes
        // Total patch size: (26 + mnU30Len) + 8 = (34 + mnU30Len)

        int add6BlockStart = 6 + mnU30Len;       // after iffalse instruction
        int ifltEnd = 14 + mnU30Len;             // after iflt instruction
        int jumpEnd = 26 + mnU30Len;             // after jump instruction
        int add14BlockStart = 26 + mnU30Len;     // start of add14 block
        int patchEnd = 34 + mnU30Len;            // end of entire patch

        // iffalse offset: skip from end of iffalse to end of patch
        // s24 = target - instructionEnd = patchEnd - (6 + mnU30Len)
        int iffalseOffset = patchEnd - (6 + mnU30Len);

        // iflt offset: skip from end of iflt to add14 block start
        // s24 = add14BlockStart - ifltEnd
        int ifltOffset = add14BlockStart - ifltEnd;

        // jump offset: skip from end of jump to end of patch
        // s24 = patchEnd - jumpEnd
        int jumpOffset = patchEnd - jumpEnd;

        // Build patch byte array
        byte[] patch = new byte[patchEnd];
        int p = 0;

        // getlocal0
        patch[p++] = (byte) 0xD0;

        // getproperty m_bHardcore
        patch[p++] = (byte) 0x66;
        System.arraycopy(mnU30, 0, patch, p, mnU30Len);
        p += mnU30Len;

        // iffalse <skip to end>
        patch[p++] = (byte) 0x12;
        writeS24(patch, p, iffalseOffset);
        p += 3;

        // getlocal 7
        patch[p++] = (byte) 0x62;
        patch[p++] = (byte) 0x07;

        // pushbyte 11
        patch[p++] = (byte) 0x24;
        patch[p++] = (byte) 0x0B;

        // iflt <to add14 block>
        patch[p++] = (byte) 0x15;
        writeS24(patch, p, ifltOffset);
        p += 3;

        // getlocal 7
        patch[p++] = (byte) 0x62;
        patch[p++] = (byte) 0x07;

        // pushbyte 6
        patch[p++] = (byte) 0x24;
        patch[p++] = (byte) 0x06;

        // add
        patch[p++] = (byte) 0xA0;

        // convert_i
        patch[p++] = (byte) 0x73;

        // setlocal 7
        patch[p++] = (byte) 0x63;
        patch[p++] = (byte) 0x07;

        // jump <to end>
        patch[p++] = (byte) 0x10;
        writeS24(patch, p, jumpOffset);
        p += 3;

        // --- add14 block ---
        // getlocal 7
        patch[p++] = (byte) 0x62;
        patch[p++] = (byte) 0x07;

        // pushbyte 14
        patch[p++] = (byte) 0x24;
        patch[p++] = (byte) 0x0E;

        // add
        patch[p++] = (byte) 0xA0;

        // convert_i
        patch[p++] = (byte) 0x73;

        // setlocal 7
        patch[p++] = (byte) 0x63;
        patch[p++] = (byte) 0x07;

        System.out.println("Patch is " + patch.length + " bytes, inserting at offset " + insertAt);

        // Build new code with patch inserted
        byte[] newCode = new byte[code.length + patch.length];
        System.arraycopy(code, 0, newCode, 0, insertAt);
        System.arraycopy(patch, 0, newCode, insertAt, patch.length);
        System.arraycopy(code, insertAt, newCode, insertAt + patch.length, code.length - insertAt);

        // Fix all existing jump offsets that cross the insertion point
        fixJumpOffsets(newCode, insertAt, patch.length);

        body.setCodeBytes(newCode);
        System.out.println("Patched SetHealth: " + code.length + " -> " + newCode.length + " bytes");
    }

    // === Helper methods ===

    static int ensureString(AVM2ConstantPool cp, String s) {
        for (int i = 1; i < cp.getStringCount(); i++) {
            if (s.equals(cp.getString(i))) return i;
        }
        return cp.addString(s);
    }

    static int findMultinameByName(AVM2ConstantPool cp, String name) {
        for (int i = 1; i < cp.getMultinameCount(); i++) {
            Multiname mn = cp.getMultiname(i);
            if (mn.name_index > 0 && name.equals(cp.getString(mn.name_index))) {
                return i;
            }
        }
        return -1;
    }

    static int addQName(AVM2ConstantPool cp, int nameIndex, int nsIndex) {
        for (int i = 1; i < cp.getMultinameCount(); i++) {
            Multiname mn = cp.getMultiname(i);
            if (mn.kind == Multiname.QNAME && mn.name_index == nameIndex && mn.namespace_index == nsIndex) {
                return i;
            }
        }
        Multiname mn = new Multiname();
        mn.kind = Multiname.QNAME;
        mn.name_index = nameIndex;
        mn.namespace_index = nsIndex;
        return cp.addMultiname(mn);
    }

    /** Encode an integer as AVM2 u30 (variable-length unsigned 30-bit integer). */
    static byte[] encodeU30(int val) {
        ByteArrayOutputStream buf = new ByteArrayOutputStream(5);
        do {
            int b = val & 0x7F;
            val >>>= 7;
            if (val != 0) b |= 0x80;
            buf.write(b);
        } while (val != 0);
        return buf.toByteArray();
    }

    // === Jump offset fixup methods (from TestIsolate.java) ===

    /**
     * Scans the bytecode for jump instructions and adjusts their s24 offsets
     * to account for insertSize bytes inserted at insertPos.
     */
    static void fixJumpOffsets(byte[] code, int insertPos, int insertSize) {
        int i = 0;
        while (i < code.length) {
            int opcode = code[i] & 0xFF;

            if (JUMP_OPCODES.contains(opcode)) {
                int jumpInstrEnd = i + 4;
                int offset = readS24(code, i + 1);

                boolean jumpIsBeforeInsert = (i < insertPos);
                boolean jumpIsAfterInsert = (i >= insertPos + insertSize);

                if (jumpIsBeforeInsert) {
                    int origTarget = jumpInstrEnd + offset;
                    if (origTarget >= insertPos) {
                        writeS24(code, i + 1, offset + insertSize);
                        System.out.println("  Fixed forward jump at " + i + ": " + offset + " -> " + (offset + insertSize));
                    }
                } else if (jumpIsAfterInsert) {
                    int origJumpEnd = (i - insertSize) + 4;
                    int origTarget = origJumpEnd + offset;
                    if (origTarget < insertPos) {
                        writeS24(code, i + 1, offset - insertSize);
                        System.out.println("  Fixed backward jump at " + i + ": " + offset + " -> " + (offset - insertSize));
                    }
                }

                i += 4;
            } else {
                i += instructionSize(code, i);
            }
        }
    }

    static int readS24(byte[] code, int pos) {
        int b0 = code[pos] & 0xFF;
        int b1 = code[pos + 1] & 0xFF;
        int b2 = code[pos + 2] & 0xFF;
        int val = b0 | (b1 << 8) | (b2 << 16);
        if ((val & 0x800000) != 0) val |= 0xFF000000;
        return val;
    }

    static void writeS24(byte[] code, int pos, int val) {
        code[pos] = (byte) (val & 0xFF);
        code[pos + 1] = (byte) ((val >> 8) & 0xFF);
        code[pos + 2] = (byte) ((val >> 16) & 0xFF);
    }

    /** Returns the byte size of the instruction at the given offset. */
    static int instructionSize(byte[] code, int pos) {
        int op = code[pos] & 0xFF;
        // No-operand instructions (1 byte)
        if (op >= 0x01 && op <= 0x0B) return 1;
        if (op == 0x1E || op == 0x1F) return 1;
        if (op >= 0x20 && op <= 0x23) return 1;
        if (op == 0x26 || op == 0x27) return 1;
        if (op == 0x28 || op == 0x29 || op == 0x2A) return 1;
        if (op == 0x2B) return 1;
        if (op == 0x30) return 1;
        if (op == 0x47 || op == 0x48) return 1;
        if (op >= 0x57 && op <= 0x5A) return 1;
        if (op >= 0x70 && op <= 0x78) return 1;
        if (op == 0x79 || op == 0x7A) return 1;
        if (op >= 0x80 && op <= 0x85) return 1;
        if (op >= 0x87 && op <= 0x99) return 1;
        if (op == 0x9A) return 1;
        if (op == 0x9B || op == 0x9C) return 1;
        if (op >= 0xA0 && op <= 0xB4) return 1;
        if (op >= 0xC0 && op <= 0xC7) return 2;
        if (op >= 0xD0 && op <= 0xD7) return 1;

        // Opcodes with s24 operand (4 bytes total)
        if (JUMP_OPCODES.contains(op)) return 4;

        // Opcodes with u30 operand
        if (op == 0x24) return 2; // pushbyte (u8)
        if (op == 0x25) return 1 + u30Len(code, pos + 1); // pushint
        if (op == 0x2C || op == 0x2D) return 1 + u30Len(code, pos + 1); // pushstring, pushint
        if (op == 0x2E || op == 0x2F) return 1 + u30Len(code, pos + 1); // pushuint, pushdouble
        if (op == 0x31) return 1 + u30Len(code, pos + 1); // pushnamespace
        if (op == 0x32) return 5; // hasnext2

        // Single u30 operand instructions
        if (op == 0x40) return 1 + u30Len(code, pos + 1); // newfunction
        if (op == 0x41 || op == 0x42) return 1 + u30Len(code, pos + 1); // call, construct
        if (op == 0x43) return 1 + u30Len(code, pos + 1) + u30Len(code, pos + 1 + u30Len(code, pos + 1)); // callmethod
        if (op == 0x44 || op == 0x45) return 1 + u30Len(code, pos + 1) + u30Len(code, pos + 1 + u30Len(code, pos + 1)); // callstatic, callsuper
        if (op == 0x46) return 1 + u30Len(code, pos + 1) + u30Len(code, pos + 1 + u30Len(code, pos + 1)); // callproperty
        if (op == 0x49) return 1 + u30Len(code, pos + 1); // constructsuper
        if (op == 0x4A) return 1 + u30Len(code, pos + 1) + u30Len(code, pos + 1 + u30Len(code, pos + 1)); // constructprop
        if (op == 0x4C) return 1 + u30Len(code, pos + 1) + u30Len(code, pos + 1 + u30Len(code, pos + 1)); // callproplex
        if (op == 0x4E) return 1 + u30Len(code, pos + 1) + u30Len(code, pos + 1 + u30Len(code, pos + 1)); // callsupervoid
        if (op == 0x4F) return 1 + u30Len(code, pos + 1) + u30Len(code, pos + 1 + u30Len(code, pos + 1)); // callpropvoid

        // Single u30 operand
        if (op == 0x53) return 1 + u30Len(code, pos + 1); // applytype
        if (op == 0x55) return 1 + u30Len(code, pos + 1); // newobject
        if (op == 0x56) return 1 + u30Len(code, pos + 1); // newarray
        if (op == 0x58) return 1 + u30Len(code, pos + 1); // newclass
        if (op == 0x59) return 1 + u30Len(code, pos + 1); // getdescendants
        if (op == 0x5A) return 1 + u30Len(code, pos + 1); // newcatch
        if (op == 0x5D || op == 0x5E) return 1 + u30Len(code, pos + 1); // findpropstrict, findproperty
        if (op == 0x60) return 1 + u30Len(code, pos + 1); // getlex
        if (op == 0x61) return 1 + u30Len(code, pos + 1); // setproperty
        if (op == 0x62) return 1 + u30Len(code, pos + 1); // getlocal
        if (op == 0x63) return 1 + u30Len(code, pos + 1); // setlocal
        if (op == 0x65) return 1 + u30Len(code, pos + 1); // getscopeobject
        if (op == 0x66) return 1 + u30Len(code, pos + 1); // getproperty
        if (op == 0x68) return 1 + u30Len(code, pos + 1); // initproperty
        if (op == 0x6A) return 1 + u30Len(code, pos + 1); // deleteproperty
        if (op == 0x6C) return 1 + u30Len(code, pos + 1); // getslot
        if (op == 0x6D) return 1 + u30Len(code, pos + 1); // setslot
        if (op == 0x86) return 1 + u30Len(code, pos + 1); // astype
        if (op == 0x80) return 1 + u30Len(code, pos + 1); // coerce

        // debug (0xEF): special format
        if (op == 0xEF) {
            int off = 1;
            off += 1; // debug_type (u8)
            off += u30Len(code, pos + off); // index (u30)
            off += 1; // reg (u8)
            off += u30Len(code, pos + off); // extra (u30)
            return off;
        }

        // lookupswitch (0x1B): s24 + u30 count + (count+1) s24 offsets
        if (op == 0x1B) {
            int off = 4; // opcode + s24 default
            int count = readU30(code, pos + off);
            off += u30Len(code, pos + off);
            off += (count + 1) * 3; // s24 offsets
            return off;
        }

        // Default: assume 1 byte
        return 1;
    }

    static int u30Len(byte[] code, int pos) {
        int len = 1;
        while ((code[pos + len - 1] & 0x80) != 0 && len < 5) len++;
        return len;
    }

    static int readU30(byte[] code, int pos) {
        int result = 0;
        int shift = 0;
        for (int i = 0; i < 5; i++) {
            int b = code[pos + i] & 0xFF;
            result |= (b & 0x7F) << shift;
            if ((b & 0x80) == 0) break;
            shift += 7;
        }
        return result;
    }
}
