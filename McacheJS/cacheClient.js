const net = require("net");
const { encodeRequest } = require("./encoder");
const { decodeResponse } = require("./decoder");

class CacheClient {

    constructor(host="127.0.0.1", port=1234) {
        this.socket = new net.Socket();
        this.host = host;
        this.port = port;
    }

    connect() {
        return new Promise((resolve) => {
            this.socket.connect(this.port, this.host, resolve);
        });
    }

    send(cmd) {
        return new Promise((resolve, reject) => {

            const data = encodeRequest(cmd);

            this.socket.write(data);

            this.socket.once("data", (res) => {
                try {
                    const decoded = decodeResponse(res);
                    resolve(decoded);
                } catch (e) {
                    reject(e);
                }
            });
        });
    }

    // commands

    get(key) {
        return this.send(["get", key]);
    }

    set(key, value) {
        return this.send(["set", key, value]);
    }

    del(key) {
        return this.send(["del", key]);
    }

    keys() {
        return this.send(["keys"]);
    }

    pexpire(key, ttl) {
        return this.send(["pexpire", key, String(ttl)]);
    }

    pttl(key) {
        return this.send(["pttl", key]);
    }

    zadd(key, score, member) {
        return this.send(["zadd", key, String(score), member]);
    }

    zrem(key, member) {
        return this.send(["zrem", key, member]);
    }

    zscore(key, member) {
        return this.send(["zscore", key, member]);
    }

    zquery(key, score, name, offset, limit) {
        return this.send([
            "zquery",
            key,
            String(score),
            name,
            String(offset),
            String(limit)
        ]);
    }
}

module.exports = CacheClient;