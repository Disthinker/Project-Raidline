import pytest

from zombie_data import calculate_zombie_threat


def test_calculate_normal_threat() -> None:
    # 测试正常数据计算
    result = calculate_zombie_threat(10, 20)
    assert result == 35.0

def test_negative_stats_raise_error() -> None:
    # 测试边界情况：输入负数时，必须抛出 ValueError
    with pytest.raises(ValueError):
        calculate_zombie_threat(-5, 10)
