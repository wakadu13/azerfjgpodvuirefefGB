#!/usr/bin/env python3
"""
test_serveur.py - Verifie le serveur de parties (zcb-server) de bout en bout.

Lance le serveur sur un port libre, puis joue les roles d'un hote (UDP), d'un
joueur navigateur (WebSocket) et d'un joueur PC (UDP) : creation de partie,
liste, rejoindre par code, relais des paquets dans les deux sens, departs,
pages web. Uniquement la bibliotheque standard de Python.

Usage : python3 server/test_serveur.py chemin/vers/zcb-server
"""
import base64, hashlib, json, os, socket, struct, subprocess, sys, tempfile, time, urllib.request

MAGIC = 0x4D42435A
LIST, HOST, STATUS, JOIN, DATA, LEAVE = 1, 2, 3, 4, 5, 6
GAMES, HOSTED, JOINED, ERROR, GONE = 0x81, 0x82, 0x83, 0x84, 0x85
LIST_MIN = 400
PROTO = 1

failures = 0


def check(cond, what):
    global failures
    print(("  ok    " if cond else "  ECHEC ") + what)
    if not cond:
        failures += 1


def head(t):
    return struct.pack('<IB', MAGIC, t)


def pstr(s):
    b = s.encode()
    return bytes([len(b)]) + b


def parse(msg):
    magic, t = struct.unpack_from('<IB', msg)
    assert magic == MAGIC
    return t, msg[5:]


def parse_games(body):
    total, players, n = struct.unpack_from('<HHB', body)
    pos, games = 5, []
    for _ in range(n):
        code, ver, pl, mx, wave, flags, plat, ln = struct.unpack_from('<HBBBHBBB', body, pos)
        pos += 10
        name = body[pos:pos + ln].decode()
        pos += ln
        games.append(dict(code=code, version=ver, players=pl, max=mx, wave=wave, flags=flags, platform=plat, name=name))
    return total, players, games


class Udp:
    def __init__(self, port):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.settimeout(1.0)
        self.addr = ('127.0.0.1', port)

    def send(self, data):
        self.s.sendto(data, self.addr)

    def recv(self, timeout=1.0):
        self.s.settimeout(timeout)
        try:
            return self.s.recvfrom(70000)[0]
        except socket.timeout:
            return None


class Ws:
    """Client WebSocket minimal (trames masquees, comme un navigateur)"""
    def __init__(self, port):
        self.s = socket.create_connection(('127.0.0.1', port))
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(('GET /ws HTTP/1.1\r\nHost: test\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\n'
                        'Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n' % key).encode())
        resp = b''
        while b'\r\n\r\n' not in resp:
            resp += self.s.recv(4096)
        head_, self.buf = resp.split(b'\r\n\r\n', 1)
        expect = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        self.ok = head_.startswith(b'HTTP/1.1 101') and ('Sec-WebSocket-Accept: ' + expect).encode() in head_

    def send(self, data, opcode=2):
        mask = os.urandom(4)
        n = len(data)
        hdr = bytes([0x80 | opcode])
        if n < 126:
            hdr += bytes([0x80 | n])
        elif n < 65536:
            hdr += bytes([0x80 | 126]) + struct.pack('>H', n)
        else:
            hdr += bytes([0x80 | 127]) + struct.pack('>Q', n)
        self.s.sendall(hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def recv(self, timeout=1.0):
        """Prochain message binaire (les pings recoivent leur pong automatiquement)"""
        end = time.time() + timeout
        while True:
            frame = self._frame()
            if frame:
                op, data = frame
                if op == 9:
                    self.send(data, 10)
                    continue
                if op == 2:
                    return data
                if op == 8:
                    return None
                continue
            left = end - time.time()
            if left <= 0:
                return None
            self.s.settimeout(left)
            try:
                chunk = self.s.recv(70000)
            except socket.timeout:
                return None
            if not chunk:
                return None
            self.buf += chunk

    def _frame(self):
        b = self.buf
        if len(b) < 2:
            return None
        op, n, h = b[0] & 15, b[1] & 127, 2
        if n == 126:
            if len(b) < 4:
                return None
            n, h = struct.unpack('>H', b[2:4])[0], 4
        elif n == 127:
            if len(b) < 10:
                return None
            n, h = struct.unpack('>Q', b[2:10])[0], 10
        if len(b) < h + n:
            return None
        self.buf = b[h + n:]
        return op, b[h:h + n]

    def close(self):
        self.s.close()


def http(port, path):
    try:
        with urllib.request.urlopen('http://127.0.0.1:%d%s' % (port, path), timeout=3) as r:
            return r.status, r.headers, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.headers, e.read()


def free_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    p = s.getsockname()[1]
    s.close()
    return p


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else './zcb-server'
    port = free_port()
    www = tempfile.mkdtemp()
    with open(os.path.join(www, 'embed.js'), 'w') as f:
        f.write('// test\n')
    srv = subprocess.Popen([exe, '--port', str(port), '--www', www], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.4)
    try:
        run(port)
    finally:
        srv.terminate()
        out, _ = srv.communicate(timeout=5)
        print('--- journal du serveur ---\n' + out)
    print('RESULTAT : %s' % ('tout est bon' if failures == 0 else '%d echec(s)' % failures))
    sys.exit(1 if failures else 0)


def run(port):
    print('Pages web')
    st, hd, body = http(port, '/api/parties')
    data = json.loads(body)
    check(st == 200 and data['parties'] == [] and data['total_parties'] == 0, '/api/parties vide au depart')
    check(hd.get('Access-Control-Allow-Origin') == '*', '/api/parties utilisable depuis un autre site (CORS)')
    st, _, body = http(port, '/')
    check(st == 200 and b'Parties en ligne' in body, '/ affiche la liste quand le jeu (index.html) est absent')
    st, _, body = http(port, '/embed.js')
    check(st == 200 and body == b'// test\n', 'fichier du dossier www servi')
    check(http(port, '/../etc/passwd')[0] == 404 and http(port, '/nexiste-pas')[0] == 404, 'chemins interdits / absents : 404')
    check(http(port, '/ws')[0] == 426, '/ws sans WebSocket refuse')

    print('Hote PC (UDP)')
    host = Udp(port)
    key = 0x1234
    host.send(head(HOST) + struct.pack('<BIBBBB', PROTO, key, 4, 50, 1, 2) + pstr('Partie de Test|<b>'))
    t, body = parse(host.recv())
    check(t == HOSTED, 'creation de partie : HOSTED')
    k, code, token = struct.unpack_from('<IHI', body)
    check(k == key and 1000 <= code <= 9999, 'code a 4 chiffres : %d' % code)
    host.send(head(HOST) + struct.pack('<BIBBBB', PROTO, key, 4, 50, 1, 2) + pstr('Partie de Test'))
    t, body = parse(host.recv())
    check(t == HOSTED and struct.unpack_from('<IHI', body) == (key, code, token), 'demande repetee : meme partie')

    print('Liste')
    lister = Udp(port)
    lister.send(head(LIST) + bytes([PROTO]))
    check(lister.recv(0.4) is None, 'demande de liste trop courte ignoree (anti-amplification)')
    lister.send((head(LIST) + bytes([PROTO])).ljust(LIST_MIN, b'\0'))
    t, body = parse(lister.recv())
    total, players, games = parse_games(body)
    check(t == GAMES and total == 1 and len(games) == 1 and games[0]['code'] == code, 'la partie est dans la liste')
    check(games[0]['name'] == 'Partie de Test<b>', 'nom nettoye (pas de |) : %r' % games[0]['name'])
    st, _, body = http(port, '/api/parties')
    data = json.loads(body)
    check(data['parties'][0]['code'] == code and data['parties'][0]['plateforme'] == 'PC Windows', '/api/parties montre la partie')
    check('\\u003cb\\u003e' in body.decode(), 'JSON sans balise html brute')

    print('Joueur navigateur (WebSocket)')
    ws = Ws(port)
    check(ws.ok, 'poignee de main WebSocket (Sec-WebSocket-Accept correct)')
    ws.send((head(LIST) + bytes([PROTO])).ljust(LIST_MIN, b'\0'))
    t, body = parse(ws.recv())
    check(t == GAMES and parse_games(body)[2][0]['code'] == code, 'liste recue par WebSocket')
    ws.send(head(JOIN) + struct.pack('<BIH', PROTO, 77, 9999 if code != 9999 else 1000))
    t, body = parse(ws.recv())
    check(t == ERROR and body[4] == 1, 'code inconnu : ERROR ZE_NOGAME')
    ws.send(head(JOIN) + struct.pack('<BIH', PROTO, 78, code))
    t, body = parse(ws.recv())
    k, c2, wsPeer, wsToken = struct.unpack_from('<IHHI', body)
    check(t == JOINED and k == 78 and c2 == code and wsPeer == 1, 'rejoint la partie : pair 1')

    ws.send(head(DATA) + struct.pack('<IHB', wsToken, 0, 0) + b'BRSH-hello')
    t, body = parse(host.recv())
    check(t == DATA and struct.unpack_from('<HB', body) == (1, 0) and body[3:] == b'BRSH-hello', 'client -> hote relaye')
    ws.send(head(DATA) + struct.pack('<IHB', wsToken ^ 0x100000, 0, 0) + b'faux')
    check(host.recv(0.3) is None, 'mauvais jeton : paquet ignore')

    print('Joueur PC (UDP) + diffusion')
    pc = Udp(port)
    pc.send(head(JOIN) + struct.pack('<BIH', PROTO, 99, code))
    t, body = parse(pc.recv())
    _, _, pcPeer, pcToken = struct.unpack_from('<IHHI', body)
    check(t == JOINED and pcPeer == 2, 'deuxieme joueur : pair 2')
    big = bytes(range(256)) * 40   # 10 Ko : un etat du monde a beaucoup de joueurs
    host.send(head(DATA) + struct.pack('<IHB', token, 0, 0) + big)
    m1, m2 = ws.recv(), pc.recv()
    check(m1 is not None and parse(m1)[1][3:] == big, 'diffusion de l\'hote recue par le navigateur (10 Ko)')
    check(m2 is not None and parse(m2)[1][3:] == big, 'diffusion de l\'hote recue par le PC (10 Ko)')
    host.send(head(DATA) + struct.pack('<IHB', token, 2, 0) + b'pour-2')
    m2 = pc.recv()
    check(m2 is not None and parse(m2)[1] == struct.pack('<HB', 0, 0) + b'pour-2', 'message adresse au pair 2')
    check(ws.recv(0.3) is None, '... et pas au pair 1')

    print('Etat de la partie')
    host.send(head(STATUS) + struct.pack('<IBBHB', token, 3, 50, 7, 1 | 2))
    t, _ = parse(host.recv())
    check(t == HOSTED, 'STATUS accuse')
    data = json.loads(http(port, '/api/parties')[2])
    g = data['parties'][0]
    check(g['joueurs'] == 3 and g['vague'] == 7 and g['en_cours'] is True, 'liste a jour (3 joueurs, vague 7)')
    host.send(head(STATUS) + struct.pack('<IBBHB', token, 3, 50, 7, 1))
    host.recv()
    check(json.loads(http(port, '/api/parties')[2])['parties'] == [], 'partie privee : retiree de la liste')
    host.send(head(STATUS) + struct.pack('<IBBHB', token, 3, 50, 7, 3))
    host.recv()
    bad = Udp(port)
    bad.send(head(STATUS) + struct.pack('<IBBHB', token, 1, 50, 1, 2))
    t, body = parse(bad.recv())
    check(t == ERROR and body[4] == 4, 'jeton vole depuis une autre adresse : ZE_UNKNOWN')

    print('Departs')
    pc.send(head(LEAVE) + struct.pack('<I', pcToken))
    t, body = parse(host.recv())
    check(t == GONE and struct.unpack_from('<H', body)[0] == 2, 'depart du PC signale a l\'hote')
    ws.close()
    t, body = parse(host.recv())
    check(t == GONE and struct.unpack_from('<H', body)[0] == 1, 'fermeture du navigateur signalee a l\'hote')
    ws2 = Ws(port)
    ws2.send(head(JOIN) + struct.pack('<BIH', PROTO, 5, code))
    t, body = parse(ws2.recv())
    check(t == JOINED, 'nouveau joueur navigateur')
    host.send(head(LEAVE) + struct.pack('<I', token))
    m = ws2.recv()
    check(m is not None and parse(m)[0] == GONE and struct.unpack_from('<H', parse(m)[1])[0] == 0, 'fin de partie signalee aux joueurs (GONE 0)')
    check(json.loads(http(port, '/api/parties')[2])['total_parties'] == 0, 'liste vide apres la fin')

    print('Partie creee depuis le navigateur')
    wsHost = Ws(port)
    wsHost.send(head(HOST) + struct.pack('<BIBBBB', PROTO, 1, 4, 50, 6, 2) + pstr(''))
    t, body = parse(wsHost.recv())
    _, wcode, wtoken = struct.unpack_from('<IHI', body)
    check(t == HOSTED, 'hote navigateur')
    data = json.loads(http(port, '/api/parties')[2])
    check(data['parties'][0]['nom'] == 'Partie %d' % wcode and data['parties'][0]['plateforme'] == 'Navigateur', 'nom par defaut "Partie <code>"')
    pc2 = Udp(port)
    pc2.send(head(JOIN) + struct.pack('<BIH', PROTO, 3, wcode))
    _, body = parse(pc2.recv())
    _, _, p2, t2 = struct.unpack_from('<IHHI', body)
    pc2.send(head(DATA) + struct.pack('<IHB', t2, 0, 0) + b'du-pc')
    m = wsHost.recv()
    check(m is not None and parse(m)[1] == struct.pack('<HB', p2, 0) + b'du-pc', 'PC -> hote navigateur relaye (jeu croise)')
    wsHost.close()
    m = pc2.recv()
    check(m is not None and parse(m)[0] == GONE, 'hote navigateur ferme : le PC est prevenu')


if __name__ == '__main__':
    main()
