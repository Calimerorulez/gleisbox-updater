# Gleisbox Updater

Zelfstandige archivering van onze bestaande herstelvariant van **Gerds
`gb2-update.c`** voor de Märklin Gleisbox. Oorspronkelijke auteur:
**Gerhard (Gerd) Bertelsmann**; zijn dank aan Karsten en zijn
[Beer-Ware-licentie](LICENSE.gerd) zijn behouden.

De eigenaar bevestigt dat de herstelvariant op echte hardware is gebruikt en
dat de Gleisbox daarna met firmware **1.70** werkte. Een oorspronkelijk
uitvoeringslog van die geslaagde update is niet teruggevonden. De bewaarde
testlogs bewijzen uitsluitend offline tests. Zie [testdekking](docs/TESTING.md).

## Welke bug is opgelost?

De oorspronkelijke `0031`-handler kon op ieder passend pingantwoord opnieuw
een reset en bootloaderquery sturen. Een herhaalde `0037`-bootidentiteit kon
daarna de blokteller opnieuw initialiseren. Dat verklaart de dubbele melding
`Start update ...`. De precieze oorzaak van de eerder waargenomen echte
bootloader-NACK `F2` is zonder volledige opname niet bewezen.

De al bestaande herstelvariant start vanuit een actieve type-`0010`-bootloader,
negeert normale pings en herhaalde identiteiten, en bewaakt UID, bootloaderhash,
blokvolgorde en ACK/NACK-fasen. De teller daalt pas na de CRC-ACK. De bestaande
20 ms pauze na iedere write, vijfsecondenbudgetten per fase, lokale CRC16/CCITT
en controle van het vaste firmwareprofiel zijn behouden. Bij fouten stopt de
overdracht zonder automatische retries. Reset en applicatiestart worden niet
verstuurd. Er is geen nieuwe updater of protocolstack ontwikkeld.

## Inhoud en privacy

- `src/gb2-recovery.c`: de bestaande herstelvariant met fictieve doel-UID.
- `tests/test_recovery.c`: de bestaande offline simulatie met fictieve IDs.
- `upstream/gb2-update.c`: ongewijzigde, op upstream gecontroleerde Gerd-bron.
- `patches/gb2-recovery.patch`: opnieuw gegenereerd verschil vanaf die bron.
- `evidence/`: geredigeerde oude offline logs, nieuwe offline logs en controles.
- [Herkomst en reproduceerbaarheid](docs/PROVENANCE.md).

**Deze publicatiekopie gebruikt de vaste fictieve UID `47000001`.** De echte
apparaat-UID is verwijderd uit broncode, tests, patch en logs. Ook hashes in
testframes zijn fictief. De vaste selectie is bewust behouden: pas de constante
`recovery_uid` en de bijbehorende logteksten lokaal aan voordat je ooit een
echte toepassing overweegt. Er is geen algemene apparaatselectie toegevoegd.
De geanonimiseerde kopie is uitsluitend offline getest, niet op hardware.

Er worden **geen binaries en geen Märklin-firmware** gepubliceerd. De oude
binary bevat de echte UID. Een build van deze publicatiekopie is door de
anonimisering een andere binary dan het oorspronkelijke hardwareprogramma.

## Bouwen en offline testen

Linux, C11-compiler en GNU make; Clang voor ASan/UBSan. Lever zelf lokaal het
rechtmatig verkregen `016-gb2.bin` voor firmware 1.70 aan. Het programma is
bewust beperkt tot het vastgelegde profiel; andere firmware wordt geweigerd.

```sh
make all
make test FIRMWARE=/pad/naar/016-gb2.bin
make sanitize FIRMWARE=/pad/naar/016-gb2.bin
make omega2 CROSS_COMPILE=/pad/naar/toolchain/bin/mipsel-openwrt-linux-
```

Firmwareprofiel: 27.063 bytes; SHA256
`11264bf8c49b428ee86fe987b4bb3092dd90b94eb5ef6d85a1ac1087e340c771`.
Het bestand wordt alleen gelezen en niet opgenomen in Git.
De code controleert grootte, header en 53 blok-CRC's; de vermelde SHA256 is
documentatiemetadata en wordt niet door de updater berekend. CRC is geen
cryptografische authenticatie.

Zonder activering voert de CLI alleen lokale controles uit:

```sh
./build/gb2-recovery /pad/naar/016-gb2.bin
```

Werkelijke CAN-activering vereist samen `--execute --bootloader -i INTERFACE`.
Het programma brengt de Gleisbox zelf niet in bootloadermodus. Dataframes
hebben geen doel-UID; de herstelroute veronderstelt één actieve bootloader en
geen andere gelijktijdige firmwareoverdracht. Zie de beperkingen in de
[testdocumentatie](docs/TESTING.md).

## Licenties

Gerds oorspronkelijke bron, de daarvan afgeleide herstelbron en de betreffende
delen van de patch vallen onder **Beer-Ware Revision 42**, met behoud van zijn
volledige vermelding. De [MIT-licentie](LICENSE) in deze repository geldt voor
de zelfstandig toegevoegde tests, buildbestanden en documentatie. Zij vervangt
Gerds licentie niet. Merknamen en firmware blijven van hun rechthebbenden.
