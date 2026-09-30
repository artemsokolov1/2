# Cavapoo для MiniFootball

Исходники скопированы из `F:\DogFootball\Assets\_Project\Art\Dogs`.
Проект MiniFootball использует Unreal Engine 5.8. Unity-файлы `.controller`,
`.mat` и `.meta` сохранены как справочные исходники; Unreal их не исполняет.

## Импорт

Из корня проекта, при закрытом редакторе:

```powershell
Set-Item -Path Env:UE-LocalDataCachePath -Value "$PWD\DerivedDataCache"
& 'F:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\MiniFootball.uproject" -run=pythonscript "-script=$PWD\Scripts\import_dogs.py" -ddc=InstalledNoZenLocalFallback -unattended -nullrhi -nosound
```

Скрипт создаёт `/Game/Characters/Dogs`: модель, общий скелет, 20 клипов,
текстуры, материал сменной формы и материалы с запечёнными Home/Away.
Повторный запуск сохраняет существующие модель и клипы и обновляет материал.
Для переимпорта изменённого FBX используйте Reimport в Unreal Editor.

## В игре

Все игроки и вратари используют Cavapoo. Рост приведён к существующей игровой
капсуле. Цвет формы берётся из выбранного комплекта в магазине/клубе;
у соперников и вратарей свои цвета. `M_Dog_Kit` выделяет зелёную область
исходной текстуры и заменяет её параметром `KitColor`, сохраняя оттенки шерсти.
Запечённые варианты доступны как `M_Dog_Cavapoo_Home` и `M_Dog_Cavapoo_Away`.

`SoccerDog.cpp` выбирает Idle/Run/Sprint по скорости, боковые шаги вратаря —
по направлению движения. Пасы, удары, игра головой, приём мяча, отборы,
подкаты, броски и ловля запускаются из соответствующих игровых действий.
Клипы действий проигрываются один раз, затем возвращается движение/ожидание.
Перемещение и контакт с мячом рассчитываются существующей игровой логикой.
Человеческие морфы лица на собаку не применяются.

Автоматическая проверка в Session Frontend → Automation:
`MiniFootball.Dogs.VisualIntegration`.

После сборки проекта импорт и проверку можно повторить одной командой:
`powershell -File Scripts/verify_dogs.ps1`.
Отчёт сохраняется в `Saved/TestReports/Dogs`, логи — в `Saved/Logs`.
