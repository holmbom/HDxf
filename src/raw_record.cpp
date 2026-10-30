#include "hdxf.hpp"
#include <cstdint>

namespace hdxf {

// Layout of data_, per group: the code as a 16 bit word little-endian, the value, '\0'.
// Values never contain '\0': ASCII values are lines, binary strings end at '\0' and
// binary chunks are held as hex text. The word: bit 15 clear, the code in bits 0-11 and the
// width of an ASCII code line that is not three columns in bits 12-14 (0 = three); a negative
// code is the int16 itself (bit 15 set, no width).

void RawRecord::append(int code, const std::string& value, int code_columns) {
    uint16_t u = static_cast<uint16_t>(static_cast<int16_t>(code));
    if (code >= 0 && code < 0x1000) {
        if (code_columns == 3 || code_columns < 0 || code_columns > 7) code_columns = 0;
        u = static_cast<uint16_t>(code | (code_columns << 12));
    }
    data_ += static_cast<char>(u & 0xFF);
    data_ += static_cast<char>((u >> 8) & 0xFF);
    data_ += value;
    data_ += '\0';
}

int RawRecord::decode_code(uint16_t word, int& code_columns) {
    if (word & 0x8000) {
        code_columns = 0;
        return static_cast<int16_t>(word);
    }
    code_columns = (word >> 12) & 7;
    return word & 0x0FFF;
}

std::vector<DxfGroup> RawRecord::groups() const {
    std::vector<DxfGroup> out;
    size_t pos = 0;
    while (pos + 2 <= data_.size()) {
        uint16_t u = static_cast<uint8_t>(data_[pos]) |
                     (static_cast<uint8_t>(data_[pos + 1]) << 8);
        pos += 2;
        size_t end = data_.find('\0', pos);
        if (end == std::string::npos) end = data_.size();
        int columns = 0;
        int code = decode_code(u, columns);
        out.push_back({code, data_.substr(pos, end - pos), columns});
        pos = end + 1;
    }
    return out;
}

} // namespace hdxf
