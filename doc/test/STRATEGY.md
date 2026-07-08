# Stratégie de Test RESTCONF

**Stratégie complète pour la qualification d'un serveur RESTCONF selon les RFCs IETF.**

---

## 🎯 OBJECTIFS

Cette stratégie définit comment **valider la conformité** d'un serveur RESTCONF aux standards IETF :

- **RFC 8040** : RESTCONF Protocol
- **RFC 8527** : NMDA (Network Management Datastore Architecture)
- **RFC 7950** : YANG 1.1 Data Modeling Language
- **RFC 7951** : JSON Encoding of YANG Data
- **RFC 6243** : Default Values
- **RFC 6415** : WebSocket Subprotocol for RESTCONF

---

## 📊 ORGANISATION DES CAMPAGNES

| Campagne | Durée | Objectif | Tests Inclus |
|----------|-------|----------|---------------|
| **Smoke** | ≈ 5 min | Vérification rapide du serveur | Infrastructure de base |
| **Sanity** | ≈ 20 min | Validation des fonctions principales | CRUD, RPC, Notifications |
| **Qualification** | ≈ 3 heures | Validation complète | **Tous les 350+ cas de test** |
| **Régression** | ≈ 4 heures | Validation après modification | Tests ayant détecté des défauts |
| **Performance** | ≈ 8 heures | Tests de charge et endurance | Charge, concurrence, mémoire |
| **Sécurité** | ≈ 2 heures | Tests de sécurité | Authentification, NACM, injection |
| **Certification RFC** | ≈ 1 journée | Validation complète | Tous les tests RFC |

---

## 🎨 NIVEAUX DE VALIDATION

Chaque fonctionnalité est validée selon **4 niveaux** :

### Niveau 1 : Validation Protocolaire
*Objectif : Vérifier que le serveur respecte les bases du protocole RESTCONF.*

**Exemples** :
- Découverte de l'API (`/.well-known/host-meta`)
- Accès au root RESTCONF (`/restconf`)
- Négociation HTTP (Content-Type, Accept)
- Codes HTTP de base (200, 404, 401, 403)

**Tests** : TC-2-001 à TC-2-020

**Module** : Aucun module spécifique nécessaire (infrastructure)

---

### Niveau 2 : Validation Fonctionnelle
*Objectif : Vérifier que les opérations RESTCONF fonctionnent correctement.*

**Exemples** :
- Opérations CRUD (GET, POST, PUT, PATCH, DELETE)
- Exécution des RPC
- Réception des notifications
- Gestion des datastores

**Tests** : TC-3-001 à TC-3-050, TC-5-001 à TC-5-040, TC-6-001 à TC-6-032

**Module requis** : `restconf-test.yang` (ou `oven.yang` pour les tests basiques)

---

### Niveau 3 : Validation Métier (Contraintes YANG)
*Objectif : Vérifier que le serveur applique correctement les contraintes du modèle YANG.*

**Exemples** :
- Contraintes `mandatory` (champs obligatoires)
- Contraintes `range` (plages de valeurs)
- Contraintes `pattern` (expressions régulières)
- Contraintes `must` (expressions XPath)
- Contraintes `when` (conditions)
- Contraintes `unique` (unicité)
- Contraintes `min-elements`/`max-elements`

**Tests** : TC-7-014 à TC-7-050

**Module requis** : `restconf-test.yang` (contient toutes les contraintes)

---

### Niveau 4 : Validation de Robustesse
*Objectif : Vérifier la stabilité et la sécurité du serveur.*

**Exemples** :
- Tests de charge (requêtes simultanées)
- Tests d'endurance (longues sessions)
- Tests de sécurité (injection, authentification)
- Tests d'erreurs (payloads invalides, URI malformées)
- Tests de récupération après incident

**Tests** : TC-7-001 à TC-7-013, TC-8-001 à TC-8-020, TC-9-001 à TC-9-020

**Module** : `restconf-test.yang` recommandé

---

## 🏗️ PHASES DE TEST

### Phase 1 : Infrastructure RESTCONF
*Objectif : Valider l'infrastructure de base du serveur.*

**Domaines** :
- Découverte de l'API (Host Metadata)
- Négociation HTTP/HTTPS
- Authentification
- Capacités annoncées

**Tests** : TC-2-001 à TC-2-040

**Module** : Aucun spécifique (infrastructure)

---

### Phase 2 : Opérations CRUD
*Objectif : Valider les opérations de manipulation des données.*

**Domaines** :
- Lecture des données (GET)
- Création (POST)
- Modification (PUT, PATCH)
- Suppression (DELETE)
- Validation des URI
- Validation des représentations (JSON/XML)

**Tests** : TC-3-001 à TC-3-050

**Module requis** : `restconf-test.yang`

---

### Phase 3 : Paramètres de Requête
*Objectif : Valider le support des paramètres RESTCONF.*

**Domaines** :
- `content` (config/nonconfig/all)
- `depth` (profondeur de l'arbre)
- `fields` (sélection de champs)
- `filter` (filtrage)
- `insert` (insertion dans les listes)
- `point` (position d'insertion)
- `with-defaults` (valeurs par défaut)
- `with-origin` (origine des données - RFC8527)

**Tests** : TC-4-001 à TC-4-055

**Module requis** : `restconf-test.yang`

---

### Phase 4 : RPC, Actions et Notifications
*Objectif : Valider les opérations spéciales.*

**Domaines** :
- Découverte des RPC
- Exécution des RPC (avec/sans paramètres)
- Gestion des erreurs RPC
- Actions YANG (YANG 1.1)
- Flux de notifications (Streams)
- Réception et format des notifications

**Tests** : TC-5-001 à TC-5-040

**Module requis** : `restconf-test.yang`

---

### Phase 5 : Datastores NMDA (RFC8527)
*Objectif : Valider le support des nouveaux datastores.*

**Domaines** :
- Découverte des datastores
- Accès à running/intended/operational
- Cohérence entre datastores
- Métadonnées d'origine (`with-origin`)
- Restrictions d'accès par datastore

**Tests** : TC-6-001 à TC-6-032

**Module requis** : `restconf-test.yang` + **Support RFC8527 sur le serveur**

---

### Phase 6 : Gestion des Erreurs
*Objectif : Valider la détection et le reporting des erreurs.*

**Domaines** :
- Codes HTTP (200, 201, 204, 400, 401, 403, 404, 405, 406, 409, 410, 412, 415, 500)
- Structure des erreurs RESTCONF
- Messages d'erreur conformes
- Absence de fuite d'information

**Tests** : TC-7-001 à TC-7-067

**Module requis** : `restconf-test.yang`

---

### Phase 7 : Sécurité
*Objectif : Valider les mécanismes de sécurité.*

**Domaines** :
- Authentification (Basic, Bearer)
- Autorisation (NACM - RFC8341)
- Confidentialité (HTTPS/TLS)
- Résistance aux attaques
- Absence de fuite d'information

**Tests** : TC-8-001 à TC-8-020

**Module** : `restconf-test.yang` recommandé

---

### Phase 8 : Performance et Robustesse
*Objectif : Valider la stabilité sous charge.*

**Domaines** :
- Tests de montée en charge
- Tests de concurrence
- Tests d'endurance
- Gestion de la mémoire
- Récupération après incident

**Tests** : TC-9-001 à TC-9-020

**Module** : `restconf-test.yang` recommandé

---

## 📋 CAS D'UTILISATION DE RÉFÉRENCE

### Exemple 1 : Le Four Sysrepo (oven.yang)

**Module** : `oven.yang` (simple, pédagogique)

**Scénario** :
1. Allumer le four (PATCH `/restconf/data/oven:oven`)
2. Régler la température (PATCH `/restconf/data/oven:oven`)
3. Vérifier la configuration (GET `/restconf/data/oven:oven`)
4. Vérifier l'état opérationnel (GET `/restconf/data/oven:oven-state`)
5. Insérer le plat (POST `/restconf/operations/oven:insert-food`)
6. Attendre la notification `oven-ready`
7. Retirer le plat (POST `/restconf/operations/oven:remove-food`)

**Fichier** : [`examples/oven/test_oven_basic.py`](examples/oven/test_oven_basic.py)

---

### Exemple 2 : Gestion d'Interfaces (restconf-test.yang)

**Module** : `restconf-test.yang` (complet)

**Scénario** :
1. Créer une interface (POST `/restconf/data/rt:restconf-test/rt:interfaces/rt:interface`)
2. Lire la configuration (GET `/restconf/data/rt:restconf-test/rt:interfaces/rt:interface=eth0`)
3. Modifier l'interface (PATCH `/restconf/data/rt:restconf-test/rt:interfaces/rt:interface=eth0`)
4. Vérifier dans les différents datastores (GET `/restconf/ds/ietf-datastores:running/...`)
5. Supprimer l'interface (DELETE `/restconf/data/rt:restconf-test/rt:interfaces/rt:interface=eth0`)

**Fichiers** : [`cases/crud/TC-3-011*`](cases/crud/)

---

## 🔄 APPROCHE DE TEST

### Approche par Itération

1. **Itération 1** : Tests Smoke (5 min)
   - Vérifier que le serveur répond
   - Vérifier la découverte de l'API
   - Vérifier l'authentification

2. **Itération 2** : Tests Sanity (20 min)
   - Vérifier les opérations CRUD de base
   - Vérifier les RPC simples
   - Vérifier les notifications basiques

3. **Itération 3** : Tests de Qualification (3h)
   - Exécuter tous les cas de test
   - Valider la conformité complète
   - Générer le rapport de conformité

4. **Itération 4** : Tests de Régression
   - Rejouer les tests ayant échoué
   - Valider les corrections

---

### Approche par Domaine

Pour une validation ciblée :

```bash
# Tester uniquement l'infrastructure
pytest doc/test/cases/infrastructure/

# Tester uniquement le CRUD
pytest doc/test/cases/crud/

# Tester uniquement les erreurs
pytest doc/test/cases/errors/
```

---

## 📊 CRITÈRES D'ACCEPTATION

### Par Phase

| Phase | Critère de Sortie |
|-------|-------------------|
| Infrastructure | Toutes les ressources obligatoires sont accessibles |
| CRUD | Toutes les méthodes CRUD fonctionnent conformément à la RFC 8040 |
| Paramètres | Tous les paramètres RFC8040 sont correctement interprétés |
| RPC/Notifications | Tous les RPC exposés sont exécutables, notifications correctes |
| NMDA | Tous les datastores annoncés sont accessibles et cohérents |
| Erreurs | Toutes les erreurs RESTCONF sont conformes à la RFC 8040 |
| Sécurité | Tous les contrôles d'accès sont correctement appliqués |
| Performance | Aucune dégradation sous charge, pas de fuite mémoire |

### Global

Un serveur est considéré **conforme RESTCONF** lorsque :

1. ✅ Toutes les phases de test sont validées
2. ✅ Aucune non-conformité bloquante n'est détectée
3. ✅ La matrice de conformité RFC est à 100%
4. ✅ Tous les messages d'erreur sont conformes
5. ✅ Aucune fuite d'information n'est observée

---

## 📈 RAPPORTS ET MÉTRIQUES

### Rapport de Conformité

Un rapport est généré après chaque campagne de test :

```
RESTCONF Qualification Report
============================

Serveur : sysrepo-restconf
Date : 2026-07-08
Module : restconf-test.yang (urn:restconf:test)

Couverture RFC8040 : 100% (40/40 tests passés)
Couverture RFC8527 : 100% (32/32 tests passés)
Couverture RFC7950 : 95% (45/48 tests passés)

Total : 347/350 tests passés (99.1%)
Échecs : 3 (TC-7-011, TC-8-015, TC-9-010)
```

### Métriques de Qualité

- **Taux de succès** : > 99%
- **Temps moyen par test** : < 500ms
- **Temps de réponse** : < 100ms
- **Mémoire utilisée** : Stable
- **CPU utilisé** : < 50%

---

## 🛠️ AUTOMATISATION

### Structure des Tests Automatiques

```
tests/
├── conftest.py                    # Fixtures pytest
├── test_infrastructure.py        # Phase 2
├── test_crud.py                   # Phase 3
├── test_parameters.py            # Phase 4
├── test_rpc.py                    # Phase 5
├── test_nmda.py                   # Phase 6
├── test_errors.py                 # Phase 7
├── test_security.py               # Phase 8
└── test_performance.py            # Phase 9
```

### Exécution

```bash
# Exécuter tous les tests
pytest tests/ -v

# Exécuter un domaine spécifique
pytest tests/test_crud.py -v

# Exécuter avec couverture
pytest tests/ --cov=src --cov-report=html

# Exécuter avec rapports
pytest tests/ -v --junitxml=report.xml --html=report.html
```

---

## 📝 CONVENTIONS

### Convention de Nommage

| Type | Format | Exemple |
|------|--------|---------|
| Cas de test | `RESTCONF-<DOMAINE>-XXX` | `RESTCONF-CRUD-001` |
| Scénario | `SQ-XXX` | `SQ-001` |
| Jeu de données | `DATA-XXX` | `DATA-001` |
| Fichier de test | `test_<domaine>.py` | `test_crud.py` |

### Convention de Priorité

| Priorité | Marqueur | Signification |
|----------|----------|----------------|
| Haute | ❗ | Bloquant, doit être corrigé |
| Moyenne | ⚠️ | Important, devrait être corrigé |
| Basse | ℹ️ | Information, peut être ignoré |

---

## 🎯 RECOMMANDATIONS

### Pour les Développeurs

1. **Commencer simple** : Utiliser `oven.yang` pour comprendre RESTCONF
2. **Passer au complet** : Utiliser `restconf-test.yang` pour la qualification
3. **Automatiser tôt** : Intégrer les tests dans la CI/CD
4. **Valider souvent** : Exécuter les tests après chaque modification

### Pour les Testeurs

1. **Lire la stratégie** avant de commencer
2. **Vérifier les prérequis** (module chargé)
3. **Suivre l'ordre des phases**
4. **Documenter les échecs** avec précision
5. **Rejouer les tests** après correction

### Pour les Intégrateurs

1. **Vérifier la conformité** avant déploiement
2. **Configurer correctement** les modules YANG
3. **Activer les logs** pour le débogage
4. **Monitorer les performances** sous charge
5. **Mettre à jour régulièrement** les modules

---

## 📞 SUPPORT

Pour toute question ou problème :

- Vérifier la [FAQ](#) (à créer)
- Consulter les [cas de test](cases/)
- Regarder les [exemples](examples/)
- Ouvrir une issue sur le dépôt

---

## 📜 HISTORIQUE

| Version | Date | Auteur | Changements |
|---------|------|--------|-------------|
| 1.0 | 2026-07-08 | - | Création initiale |
| 1.1 | 2026-07-08 | - | Réorganisation en doc/test/ |

---

## 📄 LICENSE

Ce document est sous licence MIT. Voir le fichier [LICENSE](../../LICENSE) pour plus de détails.
