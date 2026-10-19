#!/bin/bash
# Installe le serveur de la liste des parties en ligne sur un Raspberry Pi
# (ou tout Linux Debian / Ubuntu) : Mosquitto, un serveur MQTT leger.
#
# Usage (sur le Raspberry) :
#   sudo bash serveur_liste_raspberry.sh <mot de passe>
#
# Le jeu se connecte avec l'utilisateur "zcb" et ce mot de passe, et n'a le
# droit d'ecrire que dans les sujets zcb/... (annonces des parties).
# Ensuite, sur la box : rediriger le port TCP 1883 vers l'adresse locale du
# Raspberry (affichee a la fin), pour que les joueurs le joignent depuis Internet.
set -euo pipefail

PASS=${1:?"Usage : sudo bash $0 <mot de passe>"}
if [ "$(id -u)" -ne 0 ]; then echo "A lancer avec sudo"; exit 1; fi

# 1. Installation de Mosquitto (serveur) et de ses outils (tests)
apt-get update
apt-get install -y mosquitto mosquitto-clients

# 2. Compte "zcb" pour le jeu
mosquitto_passwd -b -c /etc/mosquitto/zcb.passwd zcb "$PASS"
chown mosquitto: /etc/mosquitto/zcb.passwd
chmod 600 /etc/mosquitto/zcb.passwd

# 3. Droits : le compte du jeu ne lit et n'ecrit que sous zcb/
cat > /etc/mosquitto/zcb.acl <<'EOF'
user zcb
topic readwrite zcb/#
EOF
chown mosquitto: /etc/mosquitto/zcb.acl

# 4. Configuration : ecoute sur le port 1883, connexions anonymes refusees,
#    petits messages seulement (les annonces font moins de 300 octets)
cat > /etc/mosquitto/conf.d/zcb.conf <<'EOF'
listener 1883 0.0.0.0
allow_anonymous false
password_file /etc/mosquitto/zcb.passwd
acl_file /etc/mosquitto/zcb.acl
message_size_limit 4096
max_connections 500
persistence true
EOF

systemctl enable mosquitto
systemctl restart mosquitto

# 5. Verification locale : on publie puis on relit un message de test
sleep 1
mosquitto_pub -h 127.0.0.1 -u zcb -P "$PASS" -t zcb/test -m ok -r
if mosquitto_sub -h 127.0.0.1 -u zcb -P "$PASS" -t zcb/test -C 1 -W 5 | grep -q ok; then
    echo "Serveur de la liste : OK"
else
    echo "Serveur de la liste : le test local a echoue (voir : journalctl -u mosquitto)"
    exit 1
fi
mosquitto_pub -h 127.0.0.1 -u zcb -P "$PASS" -t zcb/test -n -r   # efface le message de test

echo
echo "Adresse locale du Raspberry : $(hostname -I | awk '{print $1}')"
echo "Adresse Internet de la box  : $(curl -s https://api.ipify.org || echo '(inconnue)')"
echo
echo "Il reste a rediriger le port TCP 1883 de la box vers l'adresse locale ci-dessus."
