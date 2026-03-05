const TAG = {
    NIL: 0x00,
    ERR: 0x01,
    STR: 0x02,
    INT: 0x03,
    DBL: 0x04,
    ARR: 0x05
};

function decodeResponse(buffer) {
    let offset = 0;

    const bodyLen = buffer.readUInt32BE(offset);
    offset += 4;

    const tag = buffer.readUInt8(offset);
    offset += 1;

    if (tag === TAG.NIL) return null;

    if (tag === TAG.STR) {
        const len = buffer.readUInt32BE(offset);
        offset += 4;

        return buffer.slice(offset, offset + len).toString();
    }

    if (tag === TAG.INT) {
        return Number(buffer.readBigInt64BE(offset));
    }

    if (tag === TAG.DBL) {
        return buffer.readDoubleBE(offset);
    }

    if (tag === TAG.ERR) {
        const code = buffer.readUInt32BE(offset);
        offset += 4;

        const len = buffer.readUInt32BE(offset);
        offset += 4;

        const msg = buffer.slice(offset, offset + len).toString();
        throw new Error(`ERR ${code}: ${msg}`);
    }

    if (tag === TAG.ARR) {
        const n = buffer.readUInt32BE(offset);
        offset += 4;

        const arr = [];

        for (let i = 0; i < n; i++) {
            const type = buffer.readUInt8(offset);
            offset += 1;

            if (type === TAG.STR) {
                const len = buffer.readUInt32BE(offset);
                offset += 4;

                const str = buffer.slice(offset, offset + len).toString();
                offset += len;

                arr.push(str);
            }

            if (type === TAG.DBL) {
                const val = buffer.readDoubleBE(offset);
                offset += 8;
                arr.push(val);
            }
        }

        return arr;
    }
}

module.exports = { decodeResponse };