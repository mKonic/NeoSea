#!/usr/bin/env bash
# The Hunspell dictionaries NEO bundles (the npm dictionary-* packages, the
# versions its package-lock pins), into resources/dictionaries/<code>/ with
# each one's license. Every tarball is checked against the lock's sha512.
set -euo pipefail
cd "$(dirname "$0")/.."
out=resources/dictionaries
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$out"
while read -r code url integrity; do
    [ -z "$code" ] && continue
    if [ -s "$out/$code/index.dic" ]; then continue; fi
    curl -fsSL "$url" -o "$tmp/d.tgz"
    got="sha512-$(openssl dgst -sha512 -binary "$tmp/d.tgz" | base64 -w0)"
    if [ "$got" != "$integrity" ]; then echo "fetch-dictionaries: $code does not match its checksum" >&2; exit 1; fi
    rm -rf "$tmp/package"
    tar -xzf "$tmp/d.tgz" -C "$tmp"
    mkdir -p "$out/$code"
    cp "$tmp/package/index.aff" "$tmp/package/index.dic" "$out/$code/"
    for l in license LICENSE license.md LICENSE.md; do
        if [ -f "$tmp/package/$l" ]; then cp "$tmp/package/$l" "$out/$code/LICENSE"; break; fi
    done
    echo "fetch-dictionaries: $code"
done <<'LIST'
de https://registry.npmjs.org/dictionary-de/-/dictionary-de-3.0.0.tgz sha512-0Xbq+YpWTscAL1e18aPPaqfG4goC2o9T595L/54v2OvOPC0/TJFFlclYanxuUoK73wutM5f9EgSuWGkvQXlOXw==
el https://registry.npmjs.org/dictionary-el/-/dictionary-el-4.0.0.tgz sha512-m8dWp+AIeGnUGBOBH7gx9YeQunMYdoKXKFvdSqzKPuJjz2Tb/pdW7oxG8lIKtASLHTCsf7RZoiuQbVRHJsSNCw==
en-AU https://registry.npmjs.org/dictionary-en-au/-/dictionary-en-au-3.0.0.tgz sha512-whC8mFW7Crmoe2wH7cbBJwF6SFy3PGaExgZ1tU3J+qCUQdzJ3gdcD/PAXeWBBwAlsPA/Su0GGDxqp7uRM5lDVg==
en-CA https://registry.npmjs.org/dictionary-en-ca/-/dictionary-en-ca-3.0.0.tgz sha512-MQ4VXbPv4+jDFcQq632RL2QmB+1Raq6jH/NWKTE4IsHcNXAb/l5106/xn1Dt63Ni2dRF9qfiBkk7myA//5clTA==
en-GB https://registry.npmjs.org/dictionary-en-gb/-/dictionary-en-gb-3.0.0.tgz sha512-gU/dwC9xGYPh/0CCcmGNXO8OwSgOySAqevcSXxADCsgUD+tr6OJ52u7XaC7No7dNssvdUSfVjkzcqLrRZq3kPA==
en-US https://registry.npmjs.org/dictionary-en-us/-/dictionary-en-us-2.2.1.tgz sha512-Z8mycV0ywTfjbUTi0JZfQHqBZhu4CYFtpf7KluSGpt3xHpFlal2S/hiK50tlPynOtR5K3pG5wCradnz1yxwOHA==
es https://registry.npmjs.org/dictionary-es/-/dictionary-es-4.0.0.tgz sha512-8J3IYdaHYmkojxqhB+DneQpek6N/s3MwbVk3bPNGQSTBT5kxJtmbuiT0LSDP0Al6K9Vevo1sjspG8AGP9Yvxiw==
fr https://registry.npmjs.org/dictionary-fr/-/dictionary-fr-3.0.0.tgz sha512-fhDRX1cRyHPA69GJg8R4XgNYgSQvbNHj3kr5UqvEG1OS5pJqH6P3GbO7wpTCwvr5KMQ0NxGIsXps9P5NyXRVEg==
nl https://registry.npmjs.org/dictionary-nl/-/dictionary-nl-2.0.0.tgz sha512-BJRu3jT52mEjDunbpC9Y1UiQJy22VUuA/N6s2Oc5KuN+hHS3MDCoTQiyjifePd2rlHtZSy0fGZaB0qXiCGh7Vw==
pl https://registry.npmjs.org/dictionary-pl/-/dictionary-pl-2.0.0.tgz sha512-jg1j4DM12i6oyp34Re5UeVYZAh2u4Y0Qi8zUfLVQjRmUQA8PsHO41ShoZLDwcQm7mWfeTeZjDPLv9wygPdqj3Q==
pt-BR https://registry.npmjs.org/dictionary-pt/-/dictionary-pt-4.0.0.tgz sha512-ryJRhAtsaunBWw9grYXmVFftfab8+BCQ5wwh7fUzWO0oYEeMuR1lW2ubu1AHdvxOL32XP+u5wW/XhbFuoFuq8Q==
ro https://registry.npmjs.org/dictionary-ro/-/dictionary-ro-3.0.0.tgz sha512-kcR0BSFq16g1jCKpn15jpyio3ztMMS+O3PK4TE8ird8XjBgSAr2RRH0VoLG4uzMG1CFQNix/cHLne1wBu3CcTg==
ru https://registry.npmjs.org/dictionary-ru/-/dictionary-ru-3.0.0.tgz sha512-tigem0CDnNpxEciW6ktwVH3wWB4FYpuvpwA6IR6UlF7bVu2kez6vOejVNyZgBX1TJIjuYTCTuLxzg/YOls2mRQ==
LIST
