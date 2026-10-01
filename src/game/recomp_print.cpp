// recomp_printf for mods, implemented on the host. Body Harvest's libultra
// printf is not identified, so the format string is parsed here and each
// argument is read from the MIPS O32 variadic argument slots.
#include <cstdio>
#include <cstring>
#include <string>

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "bodyharvest_game.h"

namespace {
    // O32 passes arguments in 4-byte slots: slots 0-3 in a0-a3, the rest on the
    // caller's stack at sp + 4 * slot. 64-bit values start on an even slot.
    class VarArgs {
    public:
        VarArgs(uint8_t* rdram, recomp_context* ctx, uint32_t first_slot) : rdram(rdram), ctx(ctx), slot(first_slot) {}

        uint32_t next_u32() {
            return read_slot(slot++);
        }

        uint64_t next_u64() {
            slot = (slot + 1) & ~1u;
            uint64_t hi = read_slot(slot++);
            uint64_t lo = read_slot(slot++);
            return (hi << 32) | lo;
        }

    private:
        uint32_t read_slot(uint32_t index) const {
            if (index < 4) {
                return static_cast<uint32_t>((&ctx->r4)[index]);
            }
            return static_cast<uint32_t>(MEM_W(index * 4, ctx->r29));
        }

        uint8_t* rdram;
        recomp_context* ctx;
        uint32_t slot;
    };

    std::string read_string(uint8_t* rdram, gpr address) {
        std::string out;
        for (gpr i = 0; MEM_B(i, address) != '\0'; i++) {
            out += static_cast<char>(MEM_B(i, address));
        }
        return out;
    }

    template <typename T>
    void append_formatted(std::string& out, const std::string& spec, T value) {
        int size = std::snprintf(nullptr, 0, spec.c_str(), value);
        std::string buffer(static_cast<size_t>(size) + 1, '\0');
        std::snprintf(buffer.data(), buffer.size(), spec.c_str(), value);
        buffer.resize(static_cast<size_t>(size));
        out += buffer;
    }

    std::string format(uint8_t* rdram, const std::string& fmt, VarArgs& args) {
        std::string out;
        size_t i = 0;
        while (i < fmt.size()) {
            if (fmt[i] != '%') {
                out += fmt[i++];
                continue;
            }

            std::string spec = "%";
            i++;
            while (i < fmt.size() && std::strchr("-+ #0", fmt[i])) {
                spec += fmt[i++];
            }
            // Width and precision, with '*' taken from the arguments.
            for (int part = 0; part < 2; part++) {
                if (part == 1) {
                    if (i >= fmt.size() || fmt[i] != '.') {
                        break;
                    }
                    spec += fmt[i++];
                }
                if (i < fmt.size() && fmt[i] == '*') {
                    spec += std::to_string(static_cast<int32_t>(args.next_u32()));
                    i++;
                }
                while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') {
                    spec += fmt[i++];
                }
            }

            int long_count = 0;
            std::string short_length;
            while (i < fmt.size() && std::strchr("hlzjt", fmt[i])) {
                if (fmt[i] == 'l') {
                    long_count++;
                }
                else if (fmt[i] == 'h') {
                    short_length += 'h';
                }
                i++;
            }
            if (i >= fmt.size()) {
                out += spec;
                break;
            }

            char conv = fmt[i++];
            switch (conv) {
            case '%':
                out += '%';
                break;
            case 'd':
            case 'i':
                if (long_count >= 2) {
                    append_formatted(out, spec + "lld", static_cast<long long>(args.next_u64()));
                }
                else {
                    append_formatted(out, spec + short_length + "d", static_cast<int32_t>(args.next_u32()));
                }
                break;
            case 'u':
            case 'x':
            case 'X':
            case 'o':
                if (long_count >= 2) {
                    append_formatted(out, spec + "ll" + conv, static_cast<unsigned long long>(args.next_u64()));
                }
                else {
                    append_formatted(out, spec + short_length + conv, args.next_u32());
                }
                break;
            case 'c':
                append_formatted(out, spec + "c", static_cast<int>(args.next_u32()));
                break;
            case 'p':
                append_formatted(out, spec + "08X", args.next_u32());
                break;
            case 's':
                append_formatted(out, spec + "s", read_string(rdram, static_cast<int32_t>(args.next_u32())).c_str());
                break;
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G': {
                // Variadic floats are promoted to double.
                uint64_t bits = args.next_u64();
                double value;
                std::memcpy(&value, &bits, sizeof(value));
                append_formatted(out, spec + conv, value);
                break;
            }
            default:
                out += spec;
                out += conv;
                break;
            }
        }
        return out;
    }
}

extern "C" void recomp_printf(uint8_t* rdram, recomp_context* ctx) {
    std::string fmt = read_string(rdram, ctx->r4);
    VarArgs args{ rdram, ctx, 1 };
    std::string text = format(rdram, fmt, args);
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
    ctx->r2 = static_cast<int32_t>(text.size());
}

void bodyharvest::register_print_exports() {
    recomp::overlays::register_base_export("recomp_printf", recomp_printf);
}
