/*
 * embed.js - Integrer Zeffalie Come Back dans n'importe quel site web.
 *
 * Servi par le serveur de parties (zcb-server) a cote du jeu. Il suffit de coller,
 * la ou le jeu doit apparaitre :
 *
 *   <script src="https://jeu.exemple.fr/embed.js" async></script>
 *
 * Le script se remplace par une affiche "JOUER" au format 16/9 qui s'adapte a la
 * largeur de la page. Le jeu (quelques Mo) n'est charge qu'au clic, dans une iframe
 * (plein ecran, manette et son autorises). Il retrouve tout seul le serveur de
 * parties : celui qui sert ce script.
 *
 * Options (attributs de la balise <script>) :
 *   data-mode="parties"     liste des parties en ligne (mise a jour toutes les 5 s)
 *                           au lieu du jeu ; "Rejoindre" lance le jeu de la page
 *                           s'il y en a un, sinon l'ouvre dans un nouvel onglet
 *   data-cible="#id"        dessiner dans cet element au lieu de l'endroit du script
 *   data-largeur="960px"    largeur maximale (defaut : 960px ; "100%" = toute la largeur)
 *   data-pseudo="Toto"      nom des parties creees depuis ce site ("Partie de Toto")
 *   data-jeu="https://..."  adresse du jeu, s'il n'est pas a cote de ce script
 *   data-serveur="https://jeu.exemple.fr/"  serveur de parties, s'il n'est pas celui
 *                           qui sert ce script (jeu et script heberges ailleurs)
 *   data-direct="1"         charger le jeu tout de suite, sans affiche
 *
 * Un lien vers la page avec ?rejoindre=1234 ouvre directement la partie 1234.
 * Les styles sont isoles (Shadow DOM) : ni la page ne change le jeu, ni l'inverse.
 */
(function () {
  'use strict';
  var script = document.currentScript;
  if (!script) return;
  var opt = script.dataset;
  var base = new URL('./', script.src).href;            // dossier du script (et du jeu, par defaut)
  // Serveur de parties : celui qui sert ce script, sauf indication contraire
  var server = opt.serveur ? new URL(opt.serveur, location.href).href.replace(/\/?$/, '/') : base;
  var gameUrl = opt.jeu ? new URL(opt.jeu, location.href).href : base;
  var registry = window.ZeffalieComeBack = window.ZeffalieComeBack || { games: [] };

  var STYLE =
    ':host{display:block;width:100%;max-width:' + (opt.largeur || '960px') + ';margin:0 auto;' +
    'font:16px/1.4 system-ui,-apple-system,"Segoe UI",sans-serif;color:#f2f2f2}' +
    '.frame{position:relative;width:100%;aspect-ratio:16/9;background:#05081a;border-radius:12px;overflow:hidden;' +
    'box-shadow:0 6px 24px rgba(0,0,0,.35)}' +
    '@supports not (aspect-ratio:16/9){.frame{height:0;padding-top:56.25%}}' +
    '.frame iframe{position:absolute;inset:0;width:100%;height:100%;border:0;display:block}' +
    '.poster{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;' +
    'gap:clamp(6px,2vw,14px);text-align:center;padding:16px;cursor:pointer;border:0;width:100%;color:inherit;font:inherit;' +
    'background:radial-gradient(ellipse at 50% 30%,#1c2a5a 0%,#0b1230 55%,#05081a 100%)}' +
    '.poster:focus-visible{outline:3px solid #ffc83c;outline-offset:-6px}' +
    '.title{color:#ffc83c;font-weight:800;letter-spacing:.06em;font-size:clamp(20px,5.5vw,46px);line-height:1.05}' +
    '.sub{color:#c9cfe8;font-size:clamp(12px,2.2vw,17px);max-width:34em}' +
    '.play{display:inline-flex;align-items:center;gap:.5em;background:#ffc83c;color:#1a1300;font-weight:800;' +
    'font-size:clamp(15px,3vw,22px);padding:.55em 1.3em;border-radius:999px;box-shadow:0 4px 0 #b8870f}' +
    '.poster:hover .play{filter:brightness(1.08)}' +
    '.online{color:#5fd38d;font-size:clamp(12px,2vw,15px);min-height:1.4em}' +
    '.list{display:grid;gap:8px}' +
    '.game{display:grid;grid-template-columns:1fr auto auto;gap:12px;align-items:center;background:#0f1530;' +
    'border:1px solid #26305a;border-radius:10px;padding:10px 14px}' +
    '.name{font-weight:600;overflow-wrap:anywhere}.meta{color:#9aa3c7;font-size:14px}.live{color:#5fd38d}' +
    '.count{font-variant-numeric:tabular-nums;white-space:nowrap}.count b{font-size:19px}' +
    '.join{background:#ffc83c;color:#1a1300;border:0;border-radius:8px;font:inherit;font-weight:700;padding:8px 14px;cursor:pointer}' +
    '.join:focus-visible{outline:3px solid #fff;outline-offset:2px}' +
    '.empty{color:#9aa3c7;padding:20px;text-align:center;border:1px dashed #26305a;border-radius:10px;background:#05081a}' +
    '.head{display:flex;justify-content:space-between;align-items:baseline;gap:8px;margin:0 0 8px;flex-wrap:wrap}' +
    '.head h3{margin:0;color:#ffc83c;font-size:20px}.head span{color:#9aa3c7;font-size:14px}' +
    '@media (max-width:520px){.game{grid-template-columns:1fr auto}.count{grid-column:1}}';

  // Element hote : la cible demandee, sinon un bloc insere a la place du script
  function mount() {
    var host = opt.cible ? document.querySelector(opt.cible) : null;
    if (!host) {
      host = document.createElement('div');
      script.parentNode.insertBefore(host, script);
    }
    var root = host.attachShadow ? host.attachShadow({ mode: 'open' }) : host;
    var style = document.createElement('style');
    style.textContent = STYLE;
    root.appendChild(style);
    return root;
  }

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  }

  function fetchGames() {
    return fetch(server + 'api/parties', { cache: 'no-store' }).then(function (r) { return r.json(); });
  }

  // Le jeu : affiche, puis iframe au clic
  function gameEmbed() {
    var root = mount();
    var frame = el('div', 'frame');
    root.appendChild(frame);
    var started = false;

    function start(code) {
      var url = new URL(gameUrl);
      url.searchParams.set('embed', '1');
      if (opt.pseudo) url.searchParams.set('pseudo', opt.pseudo);
      url.searchParams.set('serveur', server);   // le jeu utilise ce serveur de parties
      if (code) url.searchParams.set('rejoindre', String(code));
      var iframe = document.createElement('iframe');
      iframe.src = url.href;
      iframe.title = 'Zeffalie Come Back';
      iframe.allow = 'fullscreen; gamepad; autoplay';
      iframe.setAttribute('allowfullscreen', '');
      iframe.addEventListener('load', function () { try { iframe.focus(); } catch (e) {} });
      frame.textContent = '';
      frame.appendChild(iframe);
      started = true;
    }

    var api = {
      // Lance le jeu (et rejoint la partie code si elle est donnee)
      play: function (code) {
        if (started && !code) return;
        start(code);
        frame.scrollIntoView({ behavior: 'smooth', block: 'center' });
      }
    };
    registry.games.push(api);

    var join = parseInt(new URLSearchParams(location.search).get('rejoindre'), 10);
    if (join >= 1000 && join <= 9999) { start(join); return; }
    if (opt.direct === '1') { start(0); return; }

    var poster = el('button', 'poster');
    poster.type = 'button';
    poster.setAttribute('aria-label', 'Jouer a Zeffalie Come Back');
    poster.appendChild(el('div', 'title', 'ZEFFALIE COME BACK'));
    poster.appendChild(el('div', 'sub', 'Protégez vos poussins des mercenaires, seul ou jusqu’à 50 en ligne.'));
    poster.appendChild(el('div', 'play', '▶  JOUER'));
    var online = el('div', 'online', '');
    poster.appendChild(online);
    poster.addEventListener('click', function () { start(0); });
    frame.appendChild(poster);
    fetchGames().then(function (d) {
      if (d.total_parties > 0)
        online.textContent = d.total_parties + (d.total_parties > 1 ? ' parties en ligne' : ' partie en ligne') +
          ' • ' + d.total_joueurs + (d.total_joueurs > 1 ? ' joueurs' : ' joueur');
    }).catch(function () {});
  }

  // La liste des parties en ligne, mise a jour toutes les 5 secondes
  function listEmbed() {
    var root = mount();
    var head = el('div', 'head');
    head.appendChild(el('h3', null, 'Parties en ligne'));
    var sum = el('span', null, '');
    head.appendChild(sum);
    var list = el('div', 'list');
    root.appendChild(head);
    root.appendChild(list);

    function joinGame(code) {
      if (registry.games.length) registry.games[0].play(code);
      else window.open(gameUrl + (gameUrl.indexOf('?') < 0 ? '?' : '&') + 'rejoindre=' + code, '_blank', 'noopener');
    }

    function render(d) {
      var games = d.parties || [];
      sum.textContent = d.total_joueurs ? d.total_joueurs + (d.total_joueurs > 1 ? ' joueurs en ligne' : ' joueur en ligne') : '';
      list.textContent = '';
      if (!games.length) { list.appendChild(el('div', 'empty', 'Aucune partie pour l’instant : lancez le jeu et créez-en une !')); return; }
      games.forEach(function (g) {
        var row = el('div', 'game'), info = el('div');
        info.appendChild(el('div', 'name', g.nom));
        var meta = el('div', 'meta', 'Code ' + g.code + ' • ' + g.plateforme + ' • ');
        meta.appendChild(g.en_cours ? el('span', 'live', 'vague ' + g.vague) : document.createTextNode('salle d’attente'));
        info.appendChild(meta);
        row.appendChild(info);
        var count = el('div', 'count');
        count.appendChild(el('b', null, String(g.joueurs)));
        count.appendChild(document.createTextNode(' / ' + g.max));
        row.appendChild(count);
        if (g.joueurs < g.max) {
          var b = el('button', 'join', 'Rejoindre');
          b.type = 'button';
          b.addEventListener('click', function () { joinGame(g.code); });
          row.appendChild(b);
        } else row.appendChild(el('span'));
        list.appendChild(row);
      });
    }

    function refresh() {
      fetchGames().then(render).catch(function () {
        list.textContent = '';
        list.appendChild(el('div', 'empty', 'Serveur de parties injoignable pour le moment.'));
      });
    }
    refresh();
    setInterval(function () { if (!document.hidden) refresh(); }, 5000);
  }

  if (opt.mode === 'parties') listEmbed();
  else gameEmbed();
})();
