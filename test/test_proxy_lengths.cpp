#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "hdxf.hpp"
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The lengths of a proxy's data and graphics. A length counted in bits (93 and 161 for the data, 96 and 162 for the
// graphics) is met by the bytes the bits need, or, when the bits fill whole bytes, by one byte more (AutoCAD's own
// form); the extra byte is kept as it is and is not counted. A length counted in bytes (92 and 160) is exact.
// ---------------------------------------------------------------------------

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

const std::vector<uint8_t> kGraphics = {0xA1, 0xB2, 0xC3, 0xD4};
const std::vector<uint8_t> kData = {0x11, 0x22, 0x33, 0x44};
const char* const kGraphicsHex = "A1B2C3D4";
const char* const kDataHex = "11223344";

const char* label(DxfVersion v) { return hdxf_test::version_label(v); }
const char* label(DxfFormat f) { return hdxf_test::format_label(f); }

hdxf::Object make_object(const std::vector<uint8_t>& data, uint32_t data_bits, const std::vector<uint8_t>& graphics,
                         uint32_t graphics_bits) {
    hdxf::Object o;
    o.type = hdxf::ObjectType::ProxyObject;
    o.handle = "FFF1";
    o.owner = "0";
    hdxf::ProxyObjectData d;
    d.proxy.application_class.dxf_name = "LENGTHS";
    d.proxy.application_class.cpp_name = "AcTestLENGTHS";
    d.proxy.application_class.application = "TestApplication";
    d.proxy.application_class.proxy_flags = 1025;
    d.proxy.drawing_format = hdxf::ProxyDrawingFormat{29, 226};
    d.proxy.original_format = 0;
    d.proxy.data = data;
    d.proxy.data_bits = data_bits;
    d.proxy.graphics = graphics;
    d.proxy.graphics_bits = graphics_bits;
    o.data = d;
    return o;
}

hdxf::Entity make_entity(const std::vector<uint8_t>& graphics) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::ProxyEntity;
    e.props.layer = "0";
    hdxf::ProxyEntityData d;
    d.proxy.application_class.dxf_name = "LENGTHS";
    d.proxy.application_class.cpp_name = "AcTestLENGTHS";
    d.proxy.application_class.application = "TestApplication";
    d.proxy.application_class.proxy_flags = 1025;
    d.proxy.application_class.entity = true;
    d.proxy.drawing_format = hdxf::ProxyDrawingFormat{29, 226};
    d.proxy.data = kData;
    d.proxy.data_bits = 32;
    d.proxy.graphics = graphics;
    d.proxy.graphics_bits = static_cast<uint32_t>(graphics.size() * 8);
    d.proxy.references.push_back({hdxf::ProxyPointerKind::HardPointer, "0"});
    d.proxy.references.push_back({hdxf::ProxyPointerKind::SoftOwner, "0"});
    e.data = d;
    return e;
}

std::vector<uint8_t> save_object(DxfVersion version, DxfFormat format, const hdxf::Object& o, std::string& error) {
    hdxf::Drawing d(version);
    d.objects().push_back(o);
    std::vector<uint8_t> out = d.save_to_memory(format);
    error = d.last_error();
    return out;
}

std::vector<uint8_t> save_entity(DxfVersion version, DxfFormat format, const hdxf::Entity& e, std::string& error) {
    hdxf::Drawing d(version);
    d.add_entity(e);
    std::vector<uint8_t> out = d.save_to_memory(format);
    error = d.last_error();
    return out;
}

const hdxf::Object* proxy_object(const hdxf::Drawing& d) {
    const hdxf::Object* found = nullptr;
    for (const hdxf::Object& o : d.objects()) {
        if (o.type == hdxf::ObjectType::ProxyObject) found = &o;
    }
    return found;
}

const hdxf::Entity* proxy_entity(const hdxf::Drawing& d) {
    for (const hdxf::Entity& e : d.entities()) {
        if (e.type == hdxf::EntityType::ProxyEntity) return &e;
    }
    return nullptr;
}

bool load(hdxf::Drawing& d, const std::vector<uint8_t>& bytes) { return d.load_from_memory(bytes.data(), bytes.size()); }

// The file's bytes with the one place that holds `hex` replaced by `replacement`
std::vector<uint8_t> edit_hex(const std::vector<uint8_t>& file, const std::string& hex, const std::string& replacement) {
    std::string text(file.begin(), file.end());
    const size_t at = text.find(hex);
    assert_true(at != std::string::npos, "the bytes " + hex + " are in the file");
    assert_true(text.find(hex, at + 1) == std::string::npos, "the bytes " + hex + " are in the file once");
    text.replace(at, hex.size(), replacement);
    return std::vector<uint8_t>(text.begin(), text.end());
}

bool warned_kept(const hdxf::Drawing& d) {
    for (const std::string& w : d.warnings()) {
        if (w.find("kept as it was read") != std::string::npos) return true;
    }
    return false;
}

std::string hex_byte(uint8_t b) {
    static const char digits[] = "0123456789ABCDEF";
    return std::string{digits[b >> 4], digits[b & 15]};
}

} // namespace

TEST(bit_lengths_accept_extra_byte) {
    // Written and read back in every form that counts bits (93 / 96 / 161 / 162): the bytes the bits fill and one more
    for (DxfVersion version : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2018}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            for (uint8_t extra : {uint8_t(0x00), uint8_t(0xCC), uint8_t(0xFF)}) {
                const std::string where = std::string(label(version)) + " " + label(format) + " extra " + hex_byte(extra);
                std::vector<uint8_t> data = kData;
                data.push_back(extra);
                std::vector<uint8_t> graphics;
                if (version >= DxfVersion::R2007) {
                    graphics = kGraphics;
                    graphics.push_back(extra);
                }
                std::string error;
                std::vector<uint8_t> first = save_object(version, format, make_object(data, 32, graphics, graphics.empty() ? 0 : 32), error);
                assert_true(!first.empty(), where + ": " + error);
                hdxf::Drawing r;
                assert_true(load(r, first), where + ": " + r.last_error());
                const hdxf::Object* o = proxy_object(r);
                assert_true(o != nullptr, where + ": the proxy is typed");
                const hdxf::ProxyClassData& p = o->as_proxy_object().proxy;
                assert_true(p.data == data, where + ": the data keep the extra byte");
                assert_eq(32, static_cast<int>(p.data_bits), where + ": the data's bits do not count it");
                assert_true(p.graphics == graphics, where + ": the graphics keep the extra byte");
                assert_eq(graphics.empty() ? 0 : 32, static_cast<int>(p.graphics_bits), where + ": the graphics' bits do not count it");
                assert_true(r.save_to_memory(format) == first, where + ": written back as it was read");
            }
        }
    }
    // Read from a file whose hex has the extra byte (the form of AutoCAD's own files): typed, kept, written back unchanged
    for (DxfVersion version : {DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2018}) {
        for (const char* extra : {"00", "CC", "FF"}) {
            const std::string where = std::string(label(version)) + " ASCII file, extra " + extra;
            std::string error;
            std::vector<uint8_t> plain = save_object(version, DxfFormat::Ascii, make_object(kData, 32, kGraphics, 32), error);
            assert_true(!plain.empty(), where + ": " + error);
            std::vector<uint8_t> file = edit_hex(plain, kGraphicsHex, std::string(kGraphicsHex) + extra);
            file = edit_hex(file, kDataHex, std::string(kDataHex) + extra);
            hdxf::Drawing r;
            assert_true(load(r, file), where + ": " + r.last_error());
            const hdxf::Object* o = proxy_object(r);
            assert_true(o != nullptr, where + ": typed, not kept as read: " + (r.warnings().empty() ? std::string() : r.warnings()[0]));
            const hdxf::ProxyClassData& p = o->as_proxy_object().proxy;
            assert_eq(5, static_cast<int>(p.graphics.size()), where + ": five graphics bytes");
            assert_eq(5, static_cast<int>(p.data.size()), where + ": five data bytes");
            assert_eq(32, static_cast<int>(p.graphics_bits), where + ": graphics bits");
            assert_eq(32, static_cast<int>(p.data_bits), where + ": data bits");
            assert_eq(std::string(extra), hex_byte(p.graphics.back()), where + ": the graphics' extra byte");
            assert_eq(std::string(extra), hex_byte(p.data.back()), where + ": the data's extra byte");
            assert_true(r.save_to_memory(DxfFormat::Ascii) == file, where + ": written back as it was read");
        }
    }
}

TEST(byte_lengths_stay_exact) {
    // 92 counts bytes: before 2007 a proxy entity's graphics are written so
    for (DxfVersion version : {DxfVersion::R2000, DxfVersion::R2004}) {
        const std::string where = label(version);
        std::string error;
        std::vector<uint8_t> plain = save_entity(version, DxfFormat::Ascii, make_entity(kGraphics), error);
        assert_true(!plain.empty(), where + ": " + error);
        {
            hdxf::Drawing r;
            assert_true(load(r, plain), where + ": " + r.last_error());
            const hdxf::Entity* e = proxy_entity(r);
            assert_true(e != nullptr, where + ": the exact length is typed");
            assert_true(e->as_proxy_entity().proxy.graphics == kGraphics, where + ": its graphics");
            assert_eq(32, static_cast<int>(e->as_proxy_entity().proxy.graphics_bits), where + ": counted as 8 bits a byte");
        }
        // one byte more than 92 says is not AutoCAD's form for a length in bytes
        for (const char* extra : {"00", "CC", "FF"}) {
            std::vector<uint8_t> file = edit_hex(plain, kGraphicsHex, std::string(kGraphicsHex) + extra);
            hdxf::Drawing r;
            assert_true(load(r, file), where + ": " + r.last_error());
            assert_true(proxy_entity(r) == nullptr, where + ": " + extra + " after a length in bytes is not accepted");
            assert_true(warned_kept(r), where + ": a warning says the record is kept as it was read");
            assert_true(r.save_to_memory(DxfFormat::Ascii) == file, where + ": and it is written back as it was");
        }
        // one byte less is not accepted either
        {
            std::vector<uint8_t> file = edit_hex(plain, kGraphicsHex, "A1B2C3");
            hdxf::Drawing r;
            assert_true(load(r, file), where + ": " + r.last_error());
            assert_true(proxy_entity(r) == nullptr, where + ": a byte short is not accepted");
        }
    }
    // 160 counts bytes as 92 does: the same record, its length under the other code
    {
        const DxfVersion version = DxfVersion::R2004;
        std::string error;
        std::vector<uint8_t> plain = save_entity(version, DxfFormat::Ascii, make_entity(kGraphics), error);
        assert_true(!plain.empty(), error);
        std::string text(plain.begin(), plain.end());
        const size_t code = text.rfind(" 92", text.find(kGraphicsHex));
        assert_true(code != std::string::npos, "the length before the bytes");
        text.replace(code, 3, "160");
        const std::vector<uint8_t> exact(text.begin(), text.end());
        hdxf::Drawing r;
        assert_true(load(r, exact), r.last_error());
        assert_true(proxy_entity(r) != nullptr, "160: the exact length is typed");
        assert_true(proxy_entity(r)->as_proxy_entity().proxy.graphics == kGraphics, "160: its graphics");
        for (const std::string& bytes : {std::string(kGraphicsHex) + "CC", std::string("A1B2C3")}) {
            hdxf::Drawing other;
            assert_true(load(other, edit_hex(exact, kGraphicsHex, bytes)), other.last_error());
            assert_true(proxy_entity(other) == nullptr, "160: " + bytes + " is not the length");
            assert_true(warned_kept(other), "160: a warning says the record is kept as it was read");
        }
    }
    // The same extra byte after a length in bits (96 / 162) is accepted, a byte short is not
    for (DxfVersion version : {DxfVersion::R2007, DxfVersion::R2010}) {
        const std::string where = label(version);
        std::string error;
        std::vector<uint8_t> plain = save_object(version, DxfFormat::Ascii, make_object(kData, 32, kGraphics, 32), error);
        assert_true(!plain.empty(), where + ": " + error);
        hdxf::Drawing with_extra;
        assert_true(load(with_extra, edit_hex(plain, kGraphicsHex, std::string(kGraphicsHex) + "CC")), with_extra.last_error());
        assert_true(proxy_object(with_extra) != nullptr, where + ": 96 / 162 take the extra byte");
        hdxf::Drawing shorter;
        assert_true(load(shorter, edit_hex(plain, kGraphicsHex, "A1B2C3")), shorter.last_error());
        assert_true(proxy_object(shorter) == nullptr, where + ": 96 / 162 do not take a byte short");
    }
}

TEST(zero_and_partial_bits_checked) {
    const DxfVersion version = DxfVersion::R2010;
    // written: zero bits hold no bytes, not even an extra one
    {
        std::string error;
        std::vector<uint8_t> out = save_object(version, DxfFormat::Ascii, make_object(kData, 32, {0xCC}, 0), error);
        assert_true(out.empty(), "graphics bytes with zero bits are refused");
        assert_true(error.find("do not hold 0 bits") != std::string::npos, error);
        out = save_object(version, DxfFormat::Ascii, make_object({0xCC}, 0, {}, 0), error);
        assert_true(out.empty(), "data bytes with zero bits are refused");
        assert_true(error.find("do not hold 0 bits") != std::string::npos, error);
    }
    // written: bits that do not fill whole bytes take exactly the bytes they need
    {
        std::string error;
        std::vector<uint8_t> five = kData;
        five.push_back(0xCC);
        std::vector<uint8_t> out = save_object(version, DxfFormat::Ascii, make_object(five, 30, {}, 0), error);
        assert_true(out.empty(), "an extra byte after 30 bits is refused");
        assert_true(error.find("do not hold 30 bits") != std::string::npos, error);
        out = save_object(version, DxfFormat::Binary, make_object(kData, 30, {}, 0), error);
        assert_true(!out.empty(), "the bytes 30 bits need are accepted: " + error);
        hdxf::Drawing r;
        assert_true(load(r, out), r.last_error());
        assert_true(proxy_object(r) != nullptr && proxy_object(r)->as_proxy_object().proxy.data_bits == 30, "30 bits are kept");
        std::vector<uint8_t> three = {0x01, 0x02, 0x03};
        out = save_object(version, DxfFormat::Ascii, make_object(three, 30, {}, 0), error);
        assert_true(out.empty(), "a byte short of 30 bits is refused");
    }
    // written: whole bytes take one byte more at most
    {
        std::string error;
        std::vector<uint8_t> six = kData;
        six.push_back(0xCC);
        six.push_back(0xCC);
        std::vector<uint8_t> out = save_object(version, DxfFormat::Ascii, make_object(six, 32, {}, 0), error);
        assert_true(out.empty(), "two bytes more than 32 bits are refused");
        assert_true(error.find("do not hold 32 bits") != std::string::npos, error);
        std::vector<uint8_t> three = {0x01, 0x02, 0x03};
        out = save_object(version, DxfFormat::Ascii, make_object(three, 32, {}, 0), error);
        assert_true(out.empty(), "a byte short of 32 bits is refused");
    }
    // read: the same rules on a file's lengths
    {
        std::string error;
        std::vector<uint8_t> exact = save_object(version, DxfFormat::Ascii, make_object(kData, 32, {}, 0), error);
        assert_true(!exact.empty(), error);
        const std::string extra = std::string(kDataHex) + "55";
        hdxf::Drawing one_more;
        assert_true(load(one_more, edit_hex(exact, kDataHex, extra)), one_more.last_error());
        assert_true(proxy_object(one_more) != nullptr, "32 bits and five bytes are read typed");
        hdxf::Drawing two_more;
        assert_true(load(two_more, edit_hex(exact, kDataHex, extra + "66")), two_more.last_error());
        assert_true(proxy_object(two_more) == nullptr, "32 bits and six bytes stay as read");
        assert_true(warned_kept(two_more), "with a warning");
        hdxf::Drawing fewer;
        assert_true(load(fewer, edit_hex(exact, kDataHex, "112233")), fewer.last_error());
        assert_true(proxy_object(fewer) == nullptr, "32 bits and three bytes stay as read");

        std::vector<uint8_t> partial = save_object(version, DxfFormat::Ascii, make_object(kData, 30, {}, 0), error);
        assert_true(!partial.empty(), error);
        hdxf::Drawing exact_bytes;
        assert_true(load(exact_bytes, partial), exact_bytes.last_error());
        assert_true(proxy_object(exact_bytes) != nullptr, "30 bits and four bytes are read typed");
        hdxf::Drawing extra_after_partial;
        assert_true(load(extra_after_partial, edit_hex(partial, kDataHex, extra)), extra_after_partial.last_error());
        assert_true(proxy_object(extra_after_partial) == nullptr, "30 bits take no extra byte");
        assert_true(warned_kept(extra_after_partial), "with a warning");
    }
}
