## TODO & BUGS

FIXED (2026-03-14):
    - /quit komento ei toimi kunnolla, quittaa serverilta mutta jaa kummittelemaan ohjelmaan sisalle
      -> Korjaus: /quit sulkee ohjelman kokonaan (ioc_.stop() + request_exit()), /disconnect katkaisee vain serveri-yhteyden

    - Tekstin valinta hiirella etta voi kopioida tekstia
      -> Korjaus: click+drag viestialueella kopioi valitut viestit leikepöydalle, double-click kopioi viestin, triple-click kopioi lahettajan + viestin

    - disabloi komennot ennenkuin oikeasti user online
      -> Korjaus: yleinen auth-gate ennen serveri-komentoja, nayttaa "Not connected" jos ei autentikoitu

    - Oikean laidan userlista ei synccaannu kunnolla kaikille aina
      -> Korjaus: set_presence() paivittaa nyt kaikkien kanavien user listat, chat-viestien lahettajat lisataan automaattisesti user listaan

    - Aakkoset vahan sekoittaa kun yrittaa kirjoittaa, kursori menee vahan mihin sattuu silloin
      -> Korjaus: cursor_byte_offset() palauttaa UTF-8 byte offsetin, input renderoidaan oikein monibyttisilla merkeilla

    - Settings sivun settings crashaa ohjelman jos hiirella klikkaa jotain setting tabia
      -> Korjaus: Checkbox ja Button komponentit luodaan kerran build_ui():ssa, ei per-render (duplikaattikomponentit FTXUI-puussa aiheutti UB)

    - /msg ei toimi kaikilla desktop kayttajilla ja viestit eivat mene perille
      -> Korjaus: pending_sends_ tukee nyt useita viesteja per vastaanottaja (vektori), 10s timeout KEY_BUNDLE:lle, virheviestit kayttajalle

    - Vaikuttaisi etta mitkaan komennot ei toimi kaverin desktop clientissa, omassa toimii
      -> Korjaus: paremmat virheviestit (failed-komennot nakyy aktiivisella kanavalla), /status komento, AUTH_FAIL nayttaa serverin virheviestin

    - Jos poistan credit ja yhdistan samalla kayttajanimella, serveri ei enaa tunnista minua, miten tama ratkaistaisiin?
      -> Korjaus: Identity key recovery salasanalla: AuthResponse.password kentta, serveri tarkistaa salasanan ja paivittaa avaimen

    - Pitka teksti voisi wrapa seuraavalle riville
      -> Korjaus: text() -> paragraph() + flex viestien renderointiin

    - Inputbox voisi menna isommaksi jos pitkaa tekstia kirjoittaa tai pastee
      -> Korjaus: dynaaminen input_lines (1-5), flexbox wrap, msg_rows skaalautuu

    - Voicechat kuntoon
      -> Korjaus: konfiguroitavat ICE-palvelimet (STUN/TURN), audio-laitteiden virheenkasittely, WebRTC failure viestit kayttajalle
