# Introduction

## 1. Présentation

Ce document constitue le référentiel de qualification d'un serveur RESTCONF
conforme aux recommandations de l'IETF.

Son objectif est de fournir une méthodologie de validation complète,
réutilisable et automatisable afin de vérifier :

- la conformité aux RFC RESTCONF ;
- la conformité aux modèles YANG ;
- la robustesse de l'implémentation ;
- la stabilité du serveur ;
- la sécurité des échanges.

Ce référentiel est indépendant de toute implémentation particulière et peut
être utilisé aussi bien pour Sysrepo, Netopeer2, OpenDaylight, ConfD que pour
des implémentations propriétaires.

---

# 2. Références

Les principaux documents de référence sont :

| RFC | Description |
|------|-------------|
| RFC8040 | RESTCONF Protocol |
| RFC7950 | YANG 1.1 |
| RFC7951 | JSON Encoding of YANG Data |
| RFC8527 | NMDA RESTCONF Extensions |
| RFC6243 | Default Values |
| RFC8341 | NACM (optionnel) |

---

# 3. Objectifs

Le référentiel couvre les domaines suivants :

- découverte RESTCONF ;
- manipulation des ressources ;
- CRUD ;
- paramètres de requête ;
- RPC ;
- Actions ;
- Notifications ;
- Datastores NMDA ;
- contraintes YANG ;
- sécurité ;
- performances ;
- robustesse.

---

# 4. Public concerné

Ce document s'adresse :

- aux développeurs RESTCONF ;
- aux équipes QA ;
- aux équipes Validation ;
- aux intégrateurs ;
- aux équipes DevOps.

---

# 5. Organisation du document

Le référentiel est organisé par domaines fonctionnels.

Chaque chapitre contient :

- les objectifs ;
- les prérequis ;
- les cas de test ;
- les critères d'acceptation ;
- les références RFC.

---

# 6. Cas d'utilisation de référence

Afin d'illustrer les différents cas de test, ce référentiel utilise le module
YANG **oven** fourni avec Sysrepo.

Ce modèle est volontairement simple mais couvre la majorité des fonctionnalités
RESTCONF.

## Structure du modèle

```
oven
├── oven
│   ├── turned-on (boolean)
│   └── temperature (uint8 0..250)
│
├── oven-state (config false)
│   ├── temperature
│   └── food-inside
│
├── insert-food (RPC)
├── remove-food (RPC)
└── oven-ready (Notification)
```

---

## Exemple de configuration

L'utilisateur souhaite :

- allumer le four ;
- régler la température à 180°C ;
- attendre que le four soit chaud ;
- insérer automatiquement le plat ;
- recevoir une notification lorsque le four est prêt ;
- retirer le plat en fin de cuisson.

Cette séquence permet d'illustrer :

- les opérations CRUD ;
- la lecture des données opérationnelles ;
- les RPC ;
- les notifications ;
- les contraintes YANG.

---

## Exemple JSON

```json
{
  "oven:oven": {
    "turned-on": true,
    "temperature": 180
  }
}
```

Cette configuration sera utilisée dans plusieurs chapitres du document.

---

# 7. Convention de nommage

Les cas de test suivent la convention :

```
RESTCONF-<DOMAINE>-XXX
```

Exemples :

```
RESTCONF-INF-001
RESTCONF-CRUD-001
RESTCONF-RPC-001
RESTCONF-NMDA-001
RESTCONF-SEC-001
RESTCONF-PERF-001
```

Cette convention garantit la stabilité des identifiants au fil des évolutions
du référentiel.

---

# 8. Philosophie

Chaque cas de test doit pouvoir être :

- exécuté manuellement ;
- automatisé avec pytest ;
- intégré dans une campagne CI/CD ;
- tracé jusqu'à une exigence de la RFC.