# Herkomst, licentie en reproduceerbaarheid

## Oorspronkelijke bron

Auteursvermelding: Gerhard (Gerd) Bertelsmann, met dank aan Karsten voor het
analysewerk. Upstream:
[GBert/railroad, can2udp/src/gb2-update.c](https://github.com/GBert/railroad/blob/a427e6528922e7f17e4eebfb9c9af68599bb2268/can2udp/src/gb2-update.c),
commit `a427e6528922e7f17e4eebfb9c9af68599bb2268`.

De bewaarde lokale referentie is op 31 oktober 2026 byte voor byte vergeleken
met deze upstreamversie. De kopie `upstream/gb2-update.c` is ongewijzigd.
SHA256: `5b0568e95555391eaf2927d5c08f56b3c8e1109099511ce495cb70de643d8377`.
Gerds volledige Beer-Ware-notice is behouden in beide C-bronnen en in
`LICENSE.gerd`. Zijn openbare auteurse-mailadres is onderdeel van die notice.

De private herstelmap bevatte de herstelbron, `test_recovery.c`, een Makefile,
een toevoegpatch, een verschil met de oorspronkelijke bron, offline testlogs,
een firmwareprofiel en native/MIPS-builds. Deze publicatie bevat een gecontroleerde
selectie en neemt geen Git-geschiedenis uit het MS2-project over.

## Publicatiewijzigingen

De herstel-FSM, CRC, blokvolgorde, timing en firmwarecontroles zijn behouden.
De echte vaste UID is vervangen door fictief `47000001`; testframes en hun
hashes zijn geanonimiseerd. Bron en tests staan in afzonderlijke directories,
de include en Makefile zijn daarop aangepast, uitvoernamen bevatten geen UID
en het firmwarepad wordt met `FIRMWARE` opgegeven. Oude logs zijn herkenbaar
geredigeerd. De oorspronkelijke toevoegpatch bevatte private gegevens; daarom
is een schoon bronverschil opnieuw gegenereerd voor deze publicatie.

Patch toepassen, uitsluitend op een eigen kopie van de referentiebron:

```sh
mkdir /tmp/gb2-patch-check
cp upstream/gb2-update.c /tmp/gb2-patch-check/gb2-update.c
patch -d /tmp/gb2-patch-check -p1 < patches/gb2-recovery.patch
cmp /tmp/gb2-patch-check/gb2-update.c src/gb2-recovery.c
```

Geen oorspronkelijke bestanden zijn overschreven. Firmware-images, echte
apparaat-UIDs, netwerkadressen van de installatie, inloggegevens, ongeredigeerde
CAN-opnames en bestaande binaries zijn niet opgenomen.

## Vergelijking van de oorspronkelijke binary

De bewaarde gestripte Omega2+-executable is 66.732 bytes, statisch gelinkt voor
ELF32 little-endian MIPS32r2/o32, soft-float. SHA256:
`69af522875a35b118a2869affac1fae8f04825461be23dba89cefec5c317a811`.

Op 31 oktober 2026 is de ongewijzigde private herstelbron met haar originele
Makefile in een aparte tijdelijke directory opnieuw gebouwd. Toolchain:
OpenWrt 25.12.1 ramips/mt76x8, GCC 14.3.0, musl, mipsel_24kc. De bestaande
`omega2`-buildregel met `-Os -g`, statische linking, `--gc-sections`,
`--build-id=none`, `-z noexecstack` en `strip --strip-all` is gebruikt.
De nieuwe gestripte executable heeft dezelfde grootte en SHA256 en is met
een volledige bytevergelijking **identiek** bevonden. De ongestripte build
verschilt; zij bevat onder meer buildpadgebonden debuginformatie.

Dit bewijst lokale reproduceerbaarheid van de bewaarde gestripte binary met
dezelfde toolchain en private bron. Het bewijst niet dat die exacte binary
destijds op hardware is uitgevoerd. De geanonimiseerde publieke bron is
bewust anders en levert dus een andere executable. Een publieke bit-identieke
rebuild van de private binary wordt niet geclaimd.

**Er wordt geen binary gepubliceerd.** De originele binary bevat een echte UID;
de geanonimiseerde build heeft geen hardwarevalidatie. Ook de statisch
meegekoppelde libc hoeft daardoor niet als binary te worden gedistribueerd.
De bestaande originele bestanden blijven lokaal intact.
