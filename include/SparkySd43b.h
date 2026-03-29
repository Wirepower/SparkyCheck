#pragma once

#if defined(SPARKYCHECK_PANEL_43B)

class SdFat32;

bool SparkySd43b_mount(void);
bool SparkySd43b_isMounted(void);
SdFat32& SparkySd43b_volume(void);

#endif
