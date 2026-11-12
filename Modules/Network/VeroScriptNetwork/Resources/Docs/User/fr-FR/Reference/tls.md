# Configuration TLS

Interrogez le contexte sélectionné avec `TlsAvailable(context)`. Les serveurs HTTPS et WSS utilisent le service de transport choisi ; HTTPS dans curl utilise sa propre implémentation TLS. Désactiver le TLS serveur de Boost ne désactive pas HTTPS dans curl.

FTlsOptions possède chaîne PEM/clé/mot de passe/trust, ensemble ≤4 Mio. Lecture explicite par l’hôte, pas de cwd ; les serveurs réutilisent le contexte TLS. FServerOptions::Tls active HTTPS, WebSocket::FOptions::Tls WSS. Client wss avec TrustPem/bUseSystemTrust ; SNI/identité suit la cible, IP vérifiée comme IP. Délais de handshake/fermeture contexte s’appliquent.

make_tls_options fournit set_trust_pem/use_system_trust/set_certificate_pem/set_private_key_pem/set_private_key_password. listen_tls est disponible dans HTTP/WS, connect_tls aussi dans WS. Fournisseurs et bibliothèques doivent correspondre à la cible ; disponibilité réelle selon déploiement natif.

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/tls.md) · [fr-FR](tls.md) · [ja-JP](../../ja-JP/Reference/tls.md) · [ko-KR](../../ko-KR/Reference/tls.md) · [zh-CN](../../zh-CN/Reference/tls.md) · [zh-TW](../../zh-TW/Reference/tls.md)
