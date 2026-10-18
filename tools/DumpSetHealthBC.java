import com.jpexs.decompiler.flash.SWF;
import com.jpexs.decompiler.flash.abc.*;
import com.jpexs.decompiler.flash.abc.avm2.AVM2ConstantPool;
import com.jpexs.decompiler.flash.abc.avm2.AVM2Code;
import com.jpexs.decompiler.flash.abc.avm2.instructions.*;
import com.jpexs.decompiler.flash.abc.types.*;
import com.jpexs.decompiler.flash.abc.types.traits.*;
import com.jpexs.decompiler.flash.tags.*;
import com.jpexs.decompiler.flash.configuration.Configuration;
import java.io.*;
import java.util.*;

/**
 * Dumps the AVM2 bytecode of the SetHealth method from a HUD SWF.
 */
public class DumpSetHealthBC {
    public static void main(String[] args) throws Exception {
        Configuration.autoDeobfuscate.set(false);
        SWF swf = new SWF(new FileInputStream(args[0]), false);

        for (Tag tag : swf.getTags()) {
            if (!(tag instanceof DoABC2Tag)) continue;
            DoABC2Tag abcTag = (DoABC2Tag) tag;
            ABC abc = abcTag.getABC();
            AVM2ConstantPool cp = abc.constants;

            // Find Hud class
            for (int c = 0; c < abc.instance_info.size(); c++) {
                Multiname mn = cp.getMultiname(abc.instance_info.get(c).name_index);
                String name = mn.name_index > 0 ? cp.getString(mn.name_index) : "";
                if (!"Hud".equals(name)) continue;

                InstanceInfo ii = abc.instance_info.get(c);

                // Find SetHealth method
                for (Trait t : ii.instance_traits.traits) {
                    if (!(t instanceof TraitMethodGetterSetter)) continue;
                    Multiname tMn = cp.getMultiname(t.name_index);
                    String tName = tMn.name_index > 0 ? cp.getString(tMn.name_index) : "";
                    if (!"SetHealth".equals(tName)) continue;

                    TraitMethodGetterSetter tm = (TraitMethodGetterSetter) t;
                    MethodBody body = abc.findBody(tm.method_info);
                    if (body == null) continue;

                    AVM2Code code = body.getCode();
                    System.out.println("=== SetHealth bytecode ===");
                    System.out.println("max_stack=" + body.max_stack + " max_regs=" + body.max_regs);
                    System.out.println();

                    for (int i = 0; i < code.code.size(); i++) {
                        AVM2Instruction inst = code.code.get(i);
                        StringBuilder sb = new StringBuilder();
                        sb.append(String.format("[%3d] offset=%d opcode=0x%02X", i, inst.getAddress(), inst.definition.instructionCode));

                        // Show operands
                        if (inst.operands != null) {
                            for (int op : inst.operands) {
                                sb.append(" ").append(op);
                            }
                        }

                        // Try to resolve multiname operands for property instructions
                        if (inst.operands != null && inst.operands.length > 0) {
                            int mnIdx = inst.operands[0];
                            int opcode = inst.definition.instructionCode;
                            // Common property opcodes that use multiname as first operand
                            if (opcode == 0x66 || opcode == 0x61 || opcode == 0x5D || opcode == 0x60 ||
                                opcode == 0x46 || opcode == 0x4F || opcode == 0x68) {
                                // getproperty, setproperty, findpropstrict, getlex, callproperty, callpropvoid, initproperty
                                try {
                                    Multiname propMn = cp.getMultiname(mnIdx);
                                    String propName = propMn.name_index > 0 ? cp.getString(propMn.name_index) : "?";
                                    sb.append(" -> ").append(propName);
                                } catch (Exception e) {}
                            }
                        }

                        // Name the opcode
                        String opName = getOpName(inst.definition.instructionCode);
                        sb.append("  (").append(opName).append(")");

                        System.out.println(sb);
                    }
                }
            }
        }
    }

    static String getOpName(int opcode) {
        switch (opcode) {
            case 0xD0: return "getlocal0";
            case 0xD1: return "getlocal1";
            case 0xD2: return "getlocal2";
            case 0xD3: return "getlocal3";
            case 0xD4: return "setlocal0";
            case 0xD5: return "setlocal1";
            case 0xD6: return "setlocal2";
            case 0xD7: return "setlocal3";
            case 0x62: return "getlocal";
            case 0x63: return "setlocal";
            case 0x30: return "pushscope";
            case 0x47: return "returnvoid";
            case 0x48: return "returnvalue";
            case 0x66: return "getproperty";
            case 0x61: return "setproperty";
            case 0x5D: return "findpropstrict";
            case 0x60: return "getlex";
            case 0x24: return "pushbyte";
            case 0x25: return "pushint";
            case 0x26: return "pushtrue";
            case 0x27: return "pushfalse";
            case 0x20: return "pushnull";
            case 0xA0: return "add";
            case 0x73: return "convert_i";
            case 0x10: return "jump";
            case 0x12: return "iffalse";
            case 0x11: return "iftrue";
            case 0x15: return "iflt";
            case 0x16: return "ifle";
            case 0x13: return "ifgt";
            case 0x0C: return "ifnlt";
            case 0x46: return "callproperty";
            case 0x4F: return "callpropvoid";
            case 0x68: return "initproperty";
            case 0x4A: return "constructprop";
            case 0x75: return "convert_d";
            case 0x29: return "pop";
            case 0x2A: return "dup";
            case 0xAB: return "equals";
            case 0xAD: return "lessthan";
            case 0xAE: return "lessequals";
            case 0xAF: return "greaterthan";
            case 0xB0: return "greaterequals";
            case 0x96: return "not";
            case 0xA8: return "coerce_a";
            default: return String.format("0x%02X", opcode);
        }
    }
}
