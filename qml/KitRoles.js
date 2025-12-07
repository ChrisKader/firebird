// Simple copy of KitModel::Role enum values for qmllint friendliness.
// Keep in sync with kitmodel.h
// Use var so the values are visible as properties of the imported script.
var IDRole = 0x100 + 1; // Qt::UserRole + 1
var NameRole = IDRole + 1;
var TypeRole = NameRole + 1;
var FlashRole = TypeRole + 1;
var Boot1Role = FlashRole + 1;
var SnapshotRole = Boot1Role + 1;
