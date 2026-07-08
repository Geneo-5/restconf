# Couverture du modèle oven.yang

| Objet YANG | Type | Fonction | Tests |
|---|---|---|---|
| oven | container | configuration | CRUD |
| turned-on | boolean | activation | CRUD |
| temperature | typedef uint8 | température | YANG |
| oven-temperature | typedef | range 0..250 | validation |
| oven-state | container config false | état opérationnel | NMDA |
| food-inside | boolean | état cuisson | Operational |
| insert-food | RPC | insertion plat | RPC |
| remove-food | RPC | retrait plat | RPC |
| oven-ready | notification | événement | Notification |