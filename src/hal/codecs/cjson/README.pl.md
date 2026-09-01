# Integracja cJSON

Kopia źródeł cJSON jest utrzymywana w `third_party/cJSON` na dokładnym commicie
zapisanym w `third_party/cjson_version.conf`. Pliki w tym katalogu są
wrapperami, które warunkowo włączają integrację i zachowują dotychczasowe
ścieżki do nagłówków JaszczurHAL.

Do synchronizacji lub weryfikacji tej zależności służy
`scripts/ensure_cjson.sh`.
