# Testresultaten en grenzen

## Bewaarde offline resultaten — 28 oktober 2026

De oorspronkelijke native tests en Clang ASan/UBSan-tests slaagden elk met
**27.047 controles**. De geredigeerde logs staan in
`evidence/historical-native-tests.log` en
`evidence/historical-sanitize-tests.log`. Dit zijn offline logs, geen CAN-opnames
en geen uitvoeringslogs van een hardware-update. Redacties zijn gemarkeerd.

De bestaande testdriver onderschept `socket`, `write`, `send`, `clock_gettime`
en `nanosleep` met linker-wrappers. Iedere socketaanroep laat de test falen;
timing is gesimuleerd. De suite opent nul sockets en benadert geen hardware.

De suite controleert:

- Geen verzending bij standaardgebruik of onvolledige activering.
- Dubbele normale pings, bootidentiteit en herhaalde bootidentiteit zonder reset
  of herinitialisatie; blokken `0x36`/`0x35` gevolgd door een synthetische `F2`.
- Alle 53 blokken byte voor byte, selector- en CRC-frames, CRC16/CCITT met
  seed `FFFF`, 73 bytes `FF`-padding en 64 datapartjes per blok.
- Aflopende blokken `0x36..0x02`, ACK-volgorde en tellerverlaging na CRC-ACK.
- Verkeerde UID, gewijzigde bootloaderhash, ontbrekende boot-/selector-/CRC-ACK,
  te vroege ACK, verkeerd blok, writefout, onderbreking en corrupte input.
- Gesimuleerde minimale 20 ms tussen writes, vaste fasebudgetten en weigering
  van reset/startframes.

De F2-test is synthetisch: de exacte bytes van het vroegere echte F2-frame
waren bij het schrijven van de test niet beschikbaar. De code stopt bij
`F1..F6`; de suite injecteert alleen `F2`. Dit is geen bewijs dat elke NACK,
elk foutpad of alle mogelijke hardwaretiming is getest.

## Opnieuw uitgevoerd — 31 oktober 2026

In een afzonderlijke tijdelijke map zijn de ongewijzigde private bron en tests
opnieuw gebouwd: native en ASan/UBSan beide **27.047 controles geslaagd**.
Daarna is dezelfde bestaande suite op de geanonimiseerde publicatiekopie
uitgevoerd, opnieuw met **27.047 controles per serie**. Nieuwe logs staan in
`evidence/public-native-tests.log` en `evidence/public-sanitize-tests.log`.
De lokale firmware is alleen gelezen. LSan stond uit wegens de al eerder
vastgelegde ptrace-beperking; dit is geen leakcheck.

De Omega2+-builds zijn alleen gecompileerd en als ELF gecontroleerd. Ze zijn
tijdens deze archivering niet op MIPS-hardware of een emulator uitgevoerd.
Een correct toepasbare patch en een privacycontrole van alle publicatiebestanden
zijn eveneens gecontroleerd. Zie `evidence/verification.json`.

## Hardware-update naar 1.70

**Door de eigenaar bevestigd:** de bestaande herstelvariant is op hardware
gebruikt en de Gleisbox werkte daarna op firmware **1.70**. Deze bevestiging
is op 31 oktober 2026 opnieuw gegeven. De exacte uitvoeringsdatum, volledige
CAN-opname en SHA256 van de destijds daadwerkelijk uitgevoerde binary zijn
niet onafhankelijk vastgelegd in teruggevonden materiaal.

De gerichte zoekronde omvatte de oude werkmap, relevante bestanden onder de
lokale home- en tijdelijke directories, bestaande backup-/logdirectories,
oude buildartefacten en beschikbare Git-reflogs. Daarbij is geen oorspronkelijk
uitvoeringslog van de geslaagde Gleisbox-update gevonden. De beschikbare
stripped Omega2+-binary is reproduceerbaar, maar haar exacte koppeling aan
die hardware-uitvoering is daarmee niet bewezen.

Dit resultaat wordt dus vastgelegd als **eigenaarbevestiging zonder onafhankelijk
bewaard uitvoeringslog**, niet als een reproduceerbare hardwaretest. Tijdens
deze archivering is geen hardware benaderd of geflasht.

## Resterende beperkingen

De herstelroute begint in een reeds actieve type-`0010`-bootloader. De vaste
firmware is versie 1.70 en beslaat 53 blokken. Andere revisies, firmwareversies,
meerdere actieve bootloaders, spanningsuitval en herstel na elke mogelijke
onderbreking zijn niet aangetoond. De generieke `88`-ACK bevat geen bloknummer
of sessietoken: late identieke ACKs blijven een protocolbeperking.

Alle blokken bevestigd betekent dat het bootloaderprotocol de overdracht heeft
bevestigd. Het programma verifieert daarna geen draaiende applicatieversie of
duurzame flashcommit en verstuurt geen applicatiestart. De melding 1.70 op
hardware berust op de afzonderlijke bevestiging van de eigenaar.
