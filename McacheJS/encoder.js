function encodeRequest(strings) {
    const parts = [];

    const nstr = Buffer.alloc(4);
    nstr.writeUInt32BE(strings.length);
    parts.push(nstr);

    for (const s of strings) {
        const buf = Buffer.from(s);
        const len = Buffer.alloc(4);
        len.writeUInt32BE(buf.length);

        parts.push(len);
        parts.push(buf);
    }

    const body = Buffer.concat(parts);

    const frame = Buffer.alloc(4);
    frame.writeUInt32BE(body.length);

    return Buffer.concat([frame, body]);
}

module.exports = { encodeRequest };