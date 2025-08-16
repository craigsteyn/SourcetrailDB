```mermaid
erDiagram
  NODE {
    int id
    int type
    string serialized_name
  }

  SYMBOL {
    int id
    int definition_kind
  }

  FILE {
    int id
    string path
    string language
    int line_count
  }

  EDGE {
    int id
    int type
    int source_node_id
    int target_node_id
  }

  SOURCE_LOCATION {
    int id
    int file_node_id
    int start_line
    int start_col
    int end_line
    int end_col
    int type
  }

  OCCURRENCE {
    int element_id
    int source_location_id
  }

  NODE ||--|| SYMBOL : "is_symbol?"
  NODE ||--|| FILE : "is_file?"
  EDGE }o--|| NODE : "source"
  EDGE }o--|| NODE : "target"
  FILE ||--o{ SOURCE_LOCATION : "spans"
  SOURCE_LOCATION ||--o{ OCCURRENCE : "mapped"
  NODE ||--o{ OCCURRENCE : "appears"
  ```
