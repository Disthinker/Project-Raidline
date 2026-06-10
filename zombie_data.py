def calculate_zombie_threat(speed: int, damage: int) -> float:
    """
    根据丧尸的速度和攻击力计算综合威胁指数。
    """
    if speed < 0 or damage < 0:
        raise ValueError("丧尸的属性值不能为负数")

    # 威胁值算法：速度的权重更高
    threat_level = (speed * 1.5) + damage
    return threat_level
